#include "BuddyAllocator.hpp"

#include <iostream>


namespace hse::arch_os {

  unsigned int BuddyAllocator::upper_power_of_two(unsigned int x) {
      x--;
      x |= x >> 1;
      x |= x >> 2;
      x |= x >> 4;
      x |= x >> 8;
      x |= x >> 16;
      x++;
      return x;
  }

  unsigned int BuddyAllocator::log2(unsigned int n) {
      unsigned int ans = 0;
      while (n > 0) {
          n /= 2;
          ans++;
      }
      return ans;
  }

  unsigned int BuddyAllocator::block_size(unsigned int i) {
      return 1 << i;
  }

  BuddyAllocator::BuddyAllocator(void *memoryStart, std::size_t memoryLength)
          : memoryStart{memoryStart}
          , root{0, memoryStart} {
      memoryLength /= PAGESIZE;
      unsigned int additionalMemory = 0;
      while (sizeof(Node) * (memoryLength + upper_power_of_two(memoryLength)) > additionalMemory * PAGESIZE) {
          additionalMemory++;
          memoryLength--;
      }
      height = log2(memoryLength - 1);
      additionalMemoryStart = (char *) memoryStart + memoryLength * PAGESIZE;
      root = Node(memoryLength - 1, height, additionalMemoryStart);

      for (unsigned int &freelist : freelists) {
          freelist = -1;
      }
      if (collect_all_subtrees(&root, 0, height)) {
          add_block(0, height);
      }
  }

  void *BuddyAllocator::allocate(std::size_t order) {
      unsigned int block = allocate_block(order);
      if (block == -1) {
          return nullptr;
      }
      root.set_was_given(block, height, order, true);
      return get_block_pointer(block);
  }

  void BuddyAllocator::deallocate(void *memory) {
      unsigned long long shift = (char *) memory - (char *) memoryStart;
      unsigned int block = shift / PAGESIZE;

      unsigned int order = root.find_depth(block, height);
      root.set_was_given(block, height, order, false);
      add_block(block, order);
      unsigned int buddy = block ^(1 << order);
      while (root.is_available(buddy, height, order)) {
          delete_block(buddy, order);
          delete_block(block, order);
          block = std::min(block, buddy);
          order++;
          add_block(block, order);
          buddy = block ^ (1 << order);
      }
  }

  bool BuddyAllocator::collect_all_subtrees(Node *node, unsigned int pageNumber, unsigned int depth) {
      if (depth == 0) {
          return true;
      }
      if (node->right == nullptr) {
          if (collect_all_subtrees(node->left, pageNumber, depth - 1)) {
              add_block(pageNumber, depth - 1);
          }
          return false;
      }
      if (collect_all_subtrees(node->right, pageNumber + block_size(depth - 1), depth - 1)) {
          return true;
      }
      add_block(pageNumber, depth - 1);
      return false;
  }

  unsigned int BuddyAllocator::allocate_block(std::size_t order) {
      if (order > height)
          return -1;
      if (freelists[order] != -1) {
          unsigned int block = freelists[order];
          delete_block(block, order);
          return block;
      }

      unsigned int block, buddy;

      block = allocate_block(order + 1);

      if (block != -1) {
          buddy = block + (1 << order);
          add_block(buddy, order);
      }
      return block;
  }

  void *BuddyAllocator::get_block_pointer(unsigned int pageNumber) {
      if (pageNumber == -1) {
          return nullptr;
      }
      unsigned long long shift = pageNumber * PAGESIZE;
      return (char *) memoryStart + shift;
  }

  void BuddyAllocator::add_block(unsigned int pageNumber, unsigned int order) {
      root.set_available(pageNumber, height, order, true);
      if (freelists[order] == -1) {
          freelists[order] = pageNumber;
          set_block_next(pageNumber, -1);
          set_block_prev(pageNumber, -1);
          return;
      }
      set_block_prev(freelists[order], pageNumber);

      set_block_next(pageNumber, freelists[order]);
      set_block_prev(pageNumber, -1);
      freelists[order] = pageNumber;
  }

  void BuddyAllocator::delete_block(unsigned int pageNumber, unsigned int order) {
      root.set_available(pageNumber, height, order, false);
      auto[next, prev] = get_piece_next_prev(pageNumber);

      if (next != -1) {
          set_block_prev(next, prev);
      }
      if (prev != -1) {
          set_block_next(prev, next);
      } else {
          freelists[order] = next;
      }
  }

  void BuddyAllocator::set_block_next(unsigned int pageNumber, unsigned int next) {
      auto *page_ptr = (unsigned int *) get_block_pointer(pageNumber);
      *(page_ptr) = next;
  }

  void BuddyAllocator::set_block_prev(unsigned int pageNumber, unsigned int prev) {
      auto *page_ptr = (unsigned int *) get_block_pointer(pageNumber);
      *(page_ptr + 1) = prev;
  }

  std::pair<unsigned int, unsigned int> BuddyAllocator::get_piece_next_prev(unsigned int pageNumber) {
      unsigned long long shift = pageNumber * PAGESIZE;
      auto *page_ptr = (unsigned int *) ((char *) memoryStart + shift);
      return {*(page_ptr), *(page_ptr + 1)};
  }

  BuddyAllocator::Node::Node(unsigned int depth, void *additionalMemory) {
      if (depth == 0) {
          return;
      }
      left = (Node *) additionalMemory;
      *left = Node(depth - 1, (char *) left + sizeof(Node));
      right = (Node *) ((char *) additionalMemory + (sizeof(Node) << depth) - sizeof(Node));
      *right = Node(depth - 1, (char *) right + sizeof(Node));
  }

  BuddyAllocator::Node::Node(unsigned int lastPage, unsigned int depth, void *additionalMemory) {
      // если нас через эту функцию просят построить полное дерево, то значит мы должны быть в списке
      if (lastPage == (1 << depth) - 1) {
          available = true;
          if (depth == 0) {
              return;
          }
          left = (Node *) additionalMemory;
          *left = Node(depth - 1, (char *) left + sizeof(Node));
          right = (Node *) ((char *) additionalMemory + (sizeof(Node) << depth) - sizeof(Node));
          *right = Node(depth - 1, (char *) right + sizeof(Node));
          return;
      }

      if (depth == 0) {
          return;
      }
      depth--;
      if ((lastPage >> depth & 1) == 1) { // если depth бит равен 1, то надо создавать правое поддерево
          lastPage &= (1 << depth) - 1;
          left = (Node *) additionalMemory;
          *left = Node(depth, (char *) left + sizeof(Node));
          right = (Node *) ((char *) additionalMemory + (sizeof(Node) << (depth + 1)) - sizeof(Node));
          *right = Node(lastPage, depth, (char *) right + sizeof(Node));
          return;
      }
      left = (Node *) additionalMemory;
      *left = Node(lastPage, depth, (char *) left + sizeof(Node));
  }

  bool BuddyAllocator::Node::is_available(unsigned int pageNumber, unsigned int depth, unsigned int order) const {
      if (depth == order) {
          return available;
      }
      depth--;
      if ((pageNumber >> depth & 1) == 1) {
          pageNumber &= (1 << depth) - 1;
          if (right == nullptr) {
              return false;
          }
          return right->is_available(pageNumber, depth, order);
      }
      if (left == nullptr) {
          return false;
      }
      return left->is_available(pageNumber, depth, order);
  }

  void
  BuddyAllocator::Node::set_available(unsigned int pageNumber, unsigned int depth, unsigned int order, bool value) {
      if (depth == order) {
          available = value;
          return;
      }
      depth--;
      if ((pageNumber >> depth & 1) == 1) {
          pageNumber &= (1 << depth) - 1;
          right->set_available(pageNumber, depth, order, value);
          return;
      }
      left->set_available(pageNumber, depth, order, value);
  }

  void
  BuddyAllocator::Node::set_was_given(unsigned int pageNumber, unsigned int depth, unsigned int order, bool value) {
      if (depth == order) {
          was_given = value;
          return;
      }
      depth--;
      if ((pageNumber >> depth & 1) == 1) {
          pageNumber &= (1 << depth) - 1;
          right->set_was_given(pageNumber, depth, order, value);
          return;
      }
      left->set_was_given(pageNumber, depth, order, value);
  }

  unsigned int BuddyAllocator::Node::find_depth(unsigned int pageNumber, unsigned int depth) const {
      if (pageNumber == 0 && was_given) {
          return depth;
      }
      if (depth == 0) {
          return -1;
      }
      depth--;
      if ((pageNumber >> depth & 1) == 1) {
          pageNumber &= (1 << depth) - 1;
          if (right == nullptr) {
              return -1;
          }
          return right->find_depth(pageNumber, depth);
      }
      if (left == nullptr) {
          return -1;
      }
      return left->find_depth(pageNumber, depth);
  }
}
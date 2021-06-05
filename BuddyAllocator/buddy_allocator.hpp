#ifndef BUDDYALLOCATOR_BUDDY_ALLOCATOR_HPP
#define BUDDYALLOCATOR_BUDDY_ALLOCATOR_HPP

#include <cstdint>

#include <sys/user.h>
#include <utility>


namespace hse::arch_os {
  /**
   * BuddyAllocator используется в качестве базового аллокатора. В данном задании, правда, мы его
   * будем реализовывать управляя не физической памятью на уровне операционной системы, а управляя
   * регионом выделенной виртуальной памяти в рамках процесса. Промышленный аллокатор должен уметь
   * работать и набором отрезков памяти а также быть корректен в многопоточных и многопроцессорных
   * системах, но в рамках этого задания мы откинем эти требования.
   *
   * Buddy allocator позваляет выделять страницы памяти или несколько подряд идущих страниц, но не
   * произвольно:
   * 0. Выделяемая память должна соотвествовать страницам памяти, а не быть произвольным отрезком
   *    памяти соотвествующего размера.
   * 1. Число выделяемых страниц является степенью двойки, от одной (нулевая степень) до
   *    2^MAX_ORDER (MAX_ORDER). Далее показатель этой степени для выделенного отрезка будет
   *    называется его уровнем.
   * 2. Номер начальной страницы выделенного куска должен делиться на
   *    2^уровень. Если смотреть на адрес, он должен делиться на PAGESIZE << уровень.
   *
   * Таким образом возможные к выделению страницы образуют двоичное дерево, в котором относительно
   * легко находить родственные веришны (отсюда приятель, buddy) без дополнительной памяти (потому
   * что, вообще говоря, нам её откуда-то надо взять).
   *
   * Например, если аллокатору передать отрезок страниц от 1 до 10 включительно:
   *          0    1    2    3    4    5    6    7    8    9    10
   * Memory:  |----|====|====|====|====|====|====|====|====|====|
   * Level 0:      |====|====|====|====|====|====|====|====|====|
   * Level 1:           |=========|=========|=========|=========|
   * Level 2:                     |===================|
   * ...
   *
   * Если каждую свободную страницу в этом дереве поднимать до самого высокого
   * уровня и объединить вершины каждого уровня в список, то выделение и
   * освобождение памяти будет довольно эффективно.
   *
   * Например:
   *  Pages:   0    1    2    3    4    5    6    7    8    9    10
   *  Level 0: - - >|====|- - - - - - - - - - - - - - - - - - - x
   *  Level 1: - - - - ->|=========|< - - - - - - - - >|=========|
   *  Level 2: - - - - - - - - - ->|===================|- - - - x
   *  ...
   *
   *  При необходимости страницы более высокого уровня можно разделить на две более низкого, а при
   *  освобождении, наоборот, объединить до более высокого.
   *
   *  Чтобы отслеживать занятость и текущий уровень элементов дерева вам потребуется
   *  дополнительная память, воспользуйтесь фрагментом выданной аллокатору памяти. Пользоваться
   *  какой-либо другой памятью, кроме (весьма конечных) полей класса и переданной в конструктор
   *  памяти запрещено.
   */
  class BuddyAllocator {
  public:
      /**
       * Размер страницы, используемой аллокатором.
       */
      static constexpr std::size_t PAGESIZE = PAGE_SIZE;

      /**
       * Максимальный уровень выделяемой памяти.
       */
      static constexpr std::ptrdiff_t MAX_ORDER = 20;
  private:
      static constexpr std::ptrdiff_t ORDERS = MAX_ORDER + 1;
  public:

      /**
       * Создаёт аллокатор на заданном отрезке памяти. Гарантируется, что этот отрезок
       * начинается на границе страницы, заканчивается на границе странице, и простилается
       * хотя бы 2 страницы.
       * @param memoryStart начало управляемой памяти. Численно делится на `PAGESIZE`.
       * @param memoryLength длина управляемой памяти. Численно делится на `PAGESIZE`.
       *
       * Время работы: O(MAX_ORDER + memoryLength). Рекомендую посильнее поделить последнее
       * слагаемое.
       */
      BuddyAllocator(void *memoryStart, std::size_t memoryLength);

      /**
       * Выделяет память. Возращает `nullptr` в случае неудачи.
       * @param order уровень выделяемой памяти, т.е. показатель степени длины выделяемого
       *               фрагмента, измеренной в страницах.
       *
       * Время работы: O(MAX_ORDER).
       */
      void *allocate(std::size_t order);

      /**
       * Освобождает ранее выделенную память. Попытка освободить память, не полученнную до
       * этого из `allocate` этого же аллокатора или освободить повторно — неопределённое
       * поведение.
       * @param memory начало освобождаемой памяти.
       *
       * Время работы: O(MAX_ORDER).
       * */
      void deallocate(void *memory);

  private:

      static unsigned int upper_power_of_two(unsigned int x);

      static unsigned int log2(unsigned int n);

      static unsigned int block_size(unsigned int i);


      /**
       * Внутренняя реализация на ваше усмотрение.
       * Помните, что пользоваться кучей или глобальными переменными нельзя.
       */
      class Node {
      private:
          bool available = false;
          bool was_given = false;

      public:
          class Node *left = nullptr;

          class Node *right = nullptr;

          // случай, когда надо полное поддерево построить
          explicit Node(unsigned int depth, void* additionalMemory);

          Node(unsigned int lastPage, unsigned int depth, void* additionalMemory);

          [[nodiscard]] bool is_available(unsigned int pageNumber, unsigned int depth, unsigned int order) const;

          void set_available(unsigned int pageNumber, unsigned int depth, unsigned int order, bool value);

          void set_was_given(unsigned int pageNumber, unsigned int depth, unsigned int order, bool value);

          [[nodiscard]] unsigned int find_depth(unsigned int pageNumber, unsigned int depth) const;
      };

      Node root;
      unsigned int height;
      unsigned int availableMemory;
      void *memoryStart;
      unsigned int freelists[ORDERS]{};
      void *additionalMemoryStart;

      bool check_block(unsigned int pageNumber, unsigned int order) const;

      bool collect_all_subtrees(Node *node, unsigned int pageNumber, unsigned int depth);

      unsigned int allocate_block(std::size_t order);

      void *get_block_pointer(unsigned int pageNumber);

      void add_block(unsigned int pageNumber, unsigned int order);

      void delete_block(unsigned int pageNumber, unsigned int order);

      void set_block_next(unsigned int pageNumber, unsigned int next);

      void set_block_prev(unsigned int pageNumber, unsigned int prev);

      std::pair<unsigned int, unsigned int> get_piece_next_prev(unsigned int pageNumber);

      /**
       * Надо хранить двусвязный список свободных ячеек
       *
       * Случай с выдачей ячейки
       *    Надо как-то понять номер ячейки, которую мы выдали
       *      Арифметика указателей (хранить начало на память и тогда номер это begin - memory)
       *    В дереве пометить эту ячейку 1
       *
       * Случай с возврщением ячейки
       *    Надо уметь узнавать выдли мы ячейку или нет
       *      Дерево, в котором буллы
       *
       *    Надо взять невыданную парную ячейку и объеденить в одну
       *    Свободна ли ячейка? Давайте в дереве хранить есть ли ячейка в списке свободных
       */
  };
}

#endif //BUDDYALLOCATOR_BUDDY_ALLOCATOR_HPP

// Copyright (c) 2018 Kenton Varda and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "common.h"
#include "tuple.h"
#include "vector.h"
#include "function.h"

#if _MSC_VER
// Need _ReadWriteBarrier
#if _MSC_VER < 1910
#include <intrin.h>
#else
#include <intrin0.h>
#endif
#endif

namespace kj {

namespace _ {  // private

template <typename Inner, typename Mapping>
class MappedIterable;
template <typename Row>
class TableMapping;
template <typename Row, typename Inner>
using TableIterable = MappedIterable<Inner, TableMapping<Row>>;

}  // namespace _ (private)

template <typename Row, typename... Indexes>
class Table {
  // A table with one or more indexes. This is the KJ alternative to map, set, unordered_map, and
  // unordered_set.
  //
  // Unlike a traditional map, which explicitly stores key/value pairs, a Table simply stores
  // "rows" of arbitrary type, and then lets the application specify how these should be indexed.
  // Rows could be indexed on a specific struct field, or they could be indexed based on a computed
  // property. An index could be hash-based or tree-based. Multiple indexes are supported, making
  // it easy to construct a "bimap".
  //
  // The table has deterministic iteration order based on the sequence of insertions and deletions.
  // In the case of only insertions, the iteration order is the order of insertion. If deletions
  // occur, then the current last row is moved to occupy the deleted slot. This determinism is
  // intended to be reliable for the purpose of testing, etc.
  //
  // Each index is a class that looks like:
  //
  //   template <typename Key>
  //   class Index {
  //   public:
  //     kj::Maybe<size_t> insert(kj::ArrayPtr<const Row> table, size_t pos);
  //     // Called to indicate that table[pos] is a newly-added value that needs to be indexed.
  //     // If this index disallows duplicates and some other matching row already exists, then
  //     // insert() returns the index of that row -- in this case, the table will roll back the
  //     // insertion.
  //     //
  //     // Insert may throw an exception, in which case the table will roll back insertion.
  //
  //     void reserve(size_t size);
  //     // Called when Table::reserve() is called.
  //
  //     void erase(kj::ArrayPtr<const Row> table, size_t pos);
  //     // Called to indicate that table[pos] is about to be removed, so should be de-indexed.
  //     //
  //     // erase() called immediately after insert() must not throw an exception, as it may be
  //     // called during unwind.
  //
  //     void move(kj::ArrayPtr<const Row> table, size_t oldPos, size_t newPos);
  //     // Called when the value at table[oldPos] is about to be moved to table[newPos].
  //     // This should never throw; if it does the table may be corrupted.
  //
  //     class Iterator;  // Behaves like a C++ iterator over size_t values.
  //     class Iterable;  // Has begin() and end() methods returning iterators.
  //
  //     template <typename... Params>
  //     Maybe<size_t> find(kj::ArrayPtr<const Row> table, Params&&...) const;
  //     // Optional. Implements Table::find<Index>(...).
  //
  //     template <typename... Params>
  //     Iterable range(kj::ArrayPtr<const Row> table, Params&&...) const;
  //     // Optional. Implements Table::range<Index>(...).
  //
  //     Iterator begin() const;
  //     Iterator end() const;
  //     // Optional. Implements Table::ordered<Index>().
  //   };

public:
  Table();
  Table(Indexes&&... indexes);

  void reserve(size_t size);
  // Pre-allocates space for a table of the given size. Normally a Table grows by re-allocating
  // its backing array whenever more space is needed. Reserving in advance avoids redundantly
  // re-allocating as the table grows.

  size_t size() const;
  size_t capacity() const;

  void clear();

  Row* begin();
  Row* end();
  const Row* begin() const;
  const Row* end() const;

  Row& insert(Row&& row);
  Row& insert(const Row& row);
  // Inserts a new row. Throws an exception if this would violate the uniqueness constraints of any
  // of the indexes.

  template <typename Collection>
  void insertAll(Collection&& collection);
  template <typename Collection>
  void insertAll(Collection& collection);
  // Given an iterable collection of Rows, inserts all of them into this table. If the input is
  // an rvalue, the rows will be moved rather than copied.

  template <typename UpdateFunc>
  Row& upsert(Row&& row, UpdateFunc&& update);
  template <typename UpdateFunc>
  Row& upsert(const Row& row, UpdateFunc&& update);
  // Tries to insert a new row. However, if a duplicate already exists (according to some index),
  // then update(Row& existingRow, Row&& newRow) is called to modify the existing row.

  template <typename Index, typename... Params>
  kj::Maybe<Row&> find(Params&&... params);
  template <typename Index, typename... Params>
  kj::Maybe<const Row&> find(Params&&... params) const;
  // Using the given index, search for a matching row. What parameters are accepted depends on the
  // index. Not all indexes support this method -- "multimap" indexes may support only range().

  template <typename Index, typename... Params>
  auto range(Params&&... params);
  template <typename Index, typename... Params>
  auto range(Params&&... params) const;
  // Using the given index, look up a range of values, returning an iterable. What parameters are
  // accepted depends on the index. Not all indexes support this method (in particular, unique
  // indexes normally don't).

  template <typename Index>
  _::TableIterable<Row, Index&> ordered();
  template <typename Index>
  _::TableIterable<const Row, const Index&> ordered() const;
  // Returns an iterable over the whole table ordered using the given index. Not all indexes
  // support this method.

  template <typename Index, typename... Params>
  bool eraseMatch(Params&&... params);
  // Erase the row that would be matched by `find<Index>(params)`. Returns true if there was a
  // match.

  template <typename Index, typename... Params>
  size_t eraseRange(Params&&... params);
  // Erase the row that would be matched by `range<Index>(params)`. Returns the number of
  // elements erased.

  void erase(Row& row);
  // Erase the given row.
  //
  // WARNING: This invalidates all iterators, so you can't iterate over rows and erase them this
  //   way. Use `eraseAll()` for that.

  template <typename Predicate, typename = decltype(instance<Predicate>()(instance<Row&>()))>
  size_t eraseAll(Predicate&& predicate);
  // Erase all rows for which predicate(row) returns true. This scans over the entire table.

  template <typename Collection, typename = decltype(instance<Collection>().begin()), bool = true>
  size_t eraseAll(Collection&& collection);
  // Erase all rows in the given iterable collection of rows. This carefully marks rows for
  // deletion in a first pass then deletes them in a second.

  template <size_t index = 0, typename... Params>
  kj::Maybe<Row&> find(Params&&... params);
  template <size_t index = 0, typename... Params>
  kj::Maybe<const Row&> find(Params&&... params) const;
  template <size_t index = 0, typename... Params>
  auto range(Params&&... params);
  template <size_t index = 0, typename... Params>
  auto range(Params&&... params) const;
  template <size_t index = 0>
  _::TableIterable<Row, TypeOfIndex<index, Tuple<Indexes...>>&> ordered();
  template <size_t index = 0>
  _::TableIterable<const Row, const TypeOfIndex<index, Tuple<Indexes...>>&> ordered() const;
  template <size_t index = 0, typename... Params>
  bool eraseMatch(Params&&... params);
  template <size_t index = 0, typename... Params>
  size_t eraseRange(Params&&... params);
  // Methods which take an index type as a template parameter can also take an index number. This
  // is useful particularly when you have multiple indexes of the same type but different runtime
  // properties. Additionally, you can omit the template parameter altogether to use the first
  // index.

  template <size_t index = 0>
  void verify();
  // Checks the integrity of indexes, throwing an exception if there are any problems. This is
  // intended to be called within the unit test for an index.

private:
  Vector<Row> rows;
  Tuple<Indexes...> indexes;

  template <size_t index = 0, bool final = (index >= sizeof...(Indexes))>
  class Impl;

  void eraseImpl(size_t pos);
  template <typename Collection>
  size_t eraseAllImpl(Collection&& collection);
};

template <typename Callbacks>
class HashIndex;
// A Table index based on a hash table.
//
// This implementation:
// * Is based on linear probing, not chaining. It is important to use a high-quality hash function.
//   Use the KJ hashing library if possible.
// * Is limited to tables of 2^30 rows or less, mainly to allow for tighter packing with 32-bit
//   integers instead of 64-bit.
// * Caches hash codes so that each table row need only be hashed once, and never checks equality
//   unless hash codes have already been determined to be equal.
//
// The `Callbacks` type defines how to compute hash codes and equality. It should be defined like:
//
//   class Callbacks {
//   public:
//     bool matches(const Row&, const Row&) const;
//     // Returns true if the two rows are matching, for the purpose of this index.
//
//     uint hashCode(const Row&) const;
//     // Computes the hash code of the given row. Matching rows (as determined by match()) must
//     // have the same hash code. Non-matching rows should have different hash codes, to the
//     // maximum extent possible. Non-matching rows with the same hash code hurt performance.
//
//     bool matches(const Row&, ...) const;
//     uint hashCode(...) const;
//     // If you wish to support find(...) with parameters other than `const Row&`, you can do
//     // so by implementing overloads of hashCode() and match() with those parameters.
//   };
//
// If your `Callbacks` type has dynamic state, you may pass its constructor parameters as the
// consturctor parameters to `HashIndex`.

template <typename Callbacks>
class TreeIndex;
// A Table index based on a B-tree.
//
// This allows sorted iteration over rows.
//
// The `Callbacks` type defines how to compare rows. It should be defined like:
//
//   class Callbacks {
//   public:
//     bool isBefore(const Row&, const Row&) const;
//     // Returns true if the row on te left comes before the row on the right.
//
//     bool matches(const Row&, const Row&) const;
//     // Returns true if the rows "match".
//     //
//     // This could be computed by checking whether isBefore() returns false regardless of the
//     // order of the parameters, but often equality is somewhat faster to check.
//
//     bool isBefore(const Row&, ...) const;
//     bool matches(const Row&, ...) const;
//     // If you wish to support find(...) with parameters other than `const Row&`, you can do
//     // so by implementing overloads of isBefore() and isAfter() with those parameters.
//   };

// =======================================================================================
// inline implementation details

namespace _ {  // private

KJ_NORETURN(void throwDuplicateTableRow());

template <typename Dst, typename Src, typename = decltype(instance<Src>().size())>
inline void tryReserveSize(Dst& dst, Src&& src) { dst.reserve(dst.size() + src.size()); }
template <typename... Params>
inline void tryReserveSize(Params&&...) {}
// If `src` has a `.size()` method, call dst.reserve(dst.size() + src.size()).
// Otherwise, do nothing.

template <typename Inner, class Mapping>
class MappedIterator: private Mapping {
  // An iterator that wraps some other iterator and maps the values through a mapping function.
  // The type `Mapping` must define a method `map()` which performs this mapping.
  //
  // TODO(cleanup): This seems generally useful. Should we put it somewhere resuable?

public:
  template <typename... Params>
  MappedIterator(Inner inner, Params&&... params)
      : Mapping(kj::fwd<Params>(params)...), inner(inner) {}

  inline auto operator->() const { return &Mapping::map(*inner); }
  inline decltype(auto) operator* () const { return Mapping::map(*inner); }
  inline decltype(auto) operator[](size_t index) const { return Mapping::map(inner[index]); }
  inline MappedIterator& operator++() { ++inner; return *this; }
  inline MappedIterator  operator++(int) { return MappedIterator(inner++, *this); }
  inline MappedIterator& operator--() { --inner; return *this; }
  inline MappedIterator  operator--(int) { return MappedIterator(inner--, *this); }
  inline MappedIterator& operator+=(ptrdiff_t amount) { inner += amount; return *this; }
  inline MappedIterator& operator-=(ptrdiff_t amount) { inner -= amount; return *this; }
  inline MappedIterator  operator+ (ptrdiff_t amount) const {
    return MappedIterator(inner + amount, *this);
  }
  inline MappedIterator  operator- (ptrdiff_t amount) const {
    return MappedIterator(inner - amount, *this);
  }
  inline ptrdiff_t operator- (const MappedIterator& other) const { return inner - other.inner; }

  inline bool operator==(const MappedIterator& other) const { return inner == other.inner; }
  inline bool operator!=(const MappedIterator& other) const { return inner != other.inner; }
  inline bool operator<=(const MappedIterator& other) const { return inner <= other.inner; }
  inline bool operator>=(const MappedIterator& other) const { return inner >= other.inner; }
  inline bool operator< (const MappedIterator& other) const { return inner <  other.inner; }
  inline bool operator> (const MappedIterator& other) const { return inner >  other.inner; }

private:
  Inner inner;
};

template <typename Inner, typename Mapping>
class MappedIterable: private Mapping {
  // An iterator that wraps some other iterator and maps the values through a mapping function.
  // The type `Mapping` must define a method `map()` which performs this mapping.
  //
  // TODO(cleanup): This seems generally useful. Should we put it somewhere resuable?

public:
  template <typename... Params>
  MappedIterable(Inner inner, Params&&... params)
      : Mapping(kj::fwd<Params>(params)...), inner(inner) {}

  typedef Decay<decltype(instance<Inner>().begin())> InnerIterator;
  typedef MappedIterator<InnerIterator, Mapping> Iterator;
  typedef Decay<decltype(instance<const Inner>().begin())> InnerConstIterator;
  typedef MappedIterator<InnerConstIterator, Mapping> ConstIterator;

  inline Iterator begin() { return { inner.begin(), (Mapping&)*this }; }
  inline Iterator end() { return { inner.end(), (Mapping&)*this }; }
  inline ConstIterator begin() const { return { inner.begin(), (const Mapping&)*this }; }
  inline ConstIterator end() const { return { inner.end(), (const Mapping&)*this }; }

private:
  Inner inner;
};

template <typename Row>
class TableMapping {
public:
  TableMapping(Row* table): table(table) {}
  Row& map(size_t i) const { return table[i]; }

private:
  Row* table;
};

template <typename Row>
class TableUnmapping {
public:
  TableUnmapping(Row* table): table(table) {}
  size_t map(Row& row) const { return &row - table; }
  size_t map(Row* row) const { return row - table; }

private:
  Row* table;
};

}  // namespace _ (private)

template <typename Row, typename... Indexes>
template <size_t index>
class Table<Row, Indexes...>::Impl<index, false> {
public:
  static void reserve(Table<Row, Indexes...>& table, size_t size) {
    get<index>(table.indexes).reserve(size);
    Impl<index + 1>::reserve(table, size);
  }

  static void clear(Table<Row, Indexes...>& table) {
    get<index>(table.indexes).clear();
    Impl<index + 1>::clear(table);
  }

  static kj::Maybe<size_t> insert(Table<Row, Indexes...>& table, size_t pos) {
    KJ_IF_MAYBE(existing, get<index>(table.indexes).insert(table.rows.asPtr(), pos)) {
      return *existing;
    }

    bool success = false;
    KJ_DEFER(if (!success) { get<index>(table.indexes).erase(table.rows.asPtr(), pos); });
    auto result = Impl<index + 1>::insert(table, pos);
    success = result == nullptr;
    return result;
  }

  static void erase(Table<Row, Indexes...>& table, size_t pos) {
    get<index>(table.indexes).erase(table.rows.asPtr(), pos);
    Impl<index + 1>::erase(table, pos);
  }

  static void move(Table<Row, Indexes...>& table, size_t oldPos, size_t newPos) {
    get<index>(table.indexes).move(table.rows.asPtr(), oldPos, newPos);
    Impl<index + 1>::move(table, oldPos, newPos);
  }
};

template <typename Row, typename... Indexes>
template <size_t index>
class Table<Row, Indexes...>::Impl<index, true> {
public:
  static void reserve(Table<Row, Indexes...>& table, size_t size) {}
  static void clear(Table<Row, Indexes...>& table) {}
  static kj::Maybe<size_t> insert(Table<Row, Indexes...>& table, size_t pos) { return nullptr; }
  static void erase(Table<Row, Indexes...>& table, size_t pos) {}
  static void move(Table<Row, Indexes...>& table, size_t oldPos, size_t newPos) {}
};

template <typename Row, typename... Indexes>
Table<Row, Indexes...>::Table(): Table(Indexes()...) {}

template <typename Row, typename... Indexes>
Table<Row, Indexes...>::Table(Indexes&&... indexes)
    : indexes(tuple(kj::fwd<Indexes&&>(indexes)...)) {}

template <typename Row, typename... Indexes>
void Table<Row, Indexes...>::reserve(size_t size) {
  rows.reserve(size);
  Impl<>::reserve(*this, size);
}

template <typename Row, typename... Indexes>
size_t Table<Row, Indexes...>::size() const {
  return rows.size();
}
template <typename Row, typename... Indexes>
void Table<Row, Indexes...>::clear() {
  Impl<>::clear(*this);
  rows.clear();
}
template <typename Row, typename... Indexes>
size_t Table<Row, Indexes...>::capacity() const {
  return rows.capacity();
}

template <typename Row, typename... Indexes>
Row* Table<Row, Indexes...>::begin() {
  return rows.begin();
}
template <typename Row, typename... Indexes>
Row* Table<Row, Indexes...>::end() {
  return rows.end();
}
template <typename Row, typename... Indexes>
const Row* Table<Row, Indexes...>::begin() const {
  return rows.begin();
}
template <typename Row, typename... Indexes>
const Row* Table<Row, Indexes...>::end() const {
  return rows.end();
}

template <typename Row, typename... Indexes>
Row& Table<Row, Indexes...>::insert(Row&& row) {
  size_t pos = rows.size();
  Row& rowRef = rows.add(kj::mv(row));
  bool success = false;
  KJ_DEFER({ if (!success) rows.removeLast(); });
  KJ_IF_MAYBE(existing, Impl<>::insert(*this, pos)) {
    _::throwDuplicateTableRow();
  } else {
    success = true;
    return rowRef;
  }
}
template <typename Row, typename... Indexes>
Row& Table<Row, Indexes...>::insert(const Row& row) {
  return insert(kj::cp(row));
}

template <typename Row, typename... Indexes>
template <typename Collection>
void Table<Row, Indexes...>::insertAll(Collection&& collection) {
  _::tryReserveSize(*this, collection);
  for (auto& row: collection) {
    insert(kj::mv(row));
  }
}

template <typename Row, typename... Indexes>
template <typename Collection>
void Table<Row, Indexes...>::insertAll(Collection& collection) {
  _::tryReserveSize(*this, collection);
  for (auto& row: collection) {
    insert(row);
  }
}

template <typename Row, typename... Indexes>
template <typename UpdateFunc>
Row& Table<Row, Indexes...>::upsert(Row&& row, UpdateFunc&& update) {
  size_t pos = rows.size();
  Row& rowRef = rows.add(kj::mv(row));
  KJ_IF_MAYBE(existing, Impl<>::insert(*this, pos)) {
    update(rows[*existing], kj::mv(rowRef));
    rows.removeLast();
    return rows[*existing];
  } else {
    return rowRef;
  }
}
template <typename Row, typename... Indexes>
template <typename UpdateFunc>
Row& Table<Row, Indexes...>::upsert(const Row& row, UpdateFunc&& update) {
  return upsert(kj::cp(row), kj::fwd<UpdateFunc>(update));
}

template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
kj::Maybe<Row&> Table<Row, Indexes...>::find(Params&&... params) {
  return find<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
kj::Maybe<Row&> Table<Row, Indexes...>::find(Params&&... params) {
  KJ_IF_MAYBE(pos, get<index>(indexes).find(rows.asPtr(), kj::fwd<Params>(params)...)) {
    return rows[*pos];
  } else {
    return nullptr;
  }
}
template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
kj::Maybe<const Row&> Table<Row, Indexes...>::find(Params&&... params) const {
  return find<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
kj::Maybe<const Row&> Table<Row, Indexes...>::find(Params&&... params) const {
  KJ_IF_MAYBE(pos, get<index>(indexes).find(rows.asPtr(), kj::fwd<Params>(params)...)) {
    return rows[*pos];
  } else {
    return nullptr;
  }
}

template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
auto Table<Row, Indexes...>::range(Params&&... params) {
  return range<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
auto Table<Row, Indexes...>::range(Params&&... params) {
  auto inner = get<index>(indexes).range(rows.asPtr(), kj::fwd<Params>(params)...);
  return _::TableIterable<Row, decltype(inner)>(kj::mv(inner), rows.begin());
}
template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
auto Table<Row, Indexes...>::range(Params&&... params) const {
  return range<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
auto Table<Row, Indexes...>::range(Params&&... params) const {
  auto inner = get<index>(indexes).range(rows.asPtr(), kj::fwd<Params>(params)...);
  return _::TableIterable<const Row, decltype(inner)>(kj::mv(inner), rows.begin());
}

template <typename Row, typename... Indexes>
template <typename Index>
_::TableIterable<Row, Index&> Table<Row, Indexes...>::ordered() {
  return ordered<indexOfType<Index, Tuple<Indexes...>>()>();
}
template <typename Row, typename... Indexes>
template <size_t index>
_::TableIterable<Row, TypeOfIndex<index, Tuple<Indexes...>>&> Table<Row, Indexes...>::ordered() {
  return { get<index>(indexes), rows.begin() };
}
template <typename Row, typename... Indexes>
template <typename Index>
_::TableIterable<const Row, const Index&> Table<Row, Indexes...>::ordered() const {
  return ordered<indexOfType<Index, Tuple<Indexes...>>()>();
}
template <typename Row, typename... Indexes>
template <size_t index>
_::TableIterable<const Row, const TypeOfIndex<index, Tuple<Indexes...>>&>
Table<Row, Indexes...>::ordered() const {
  return { get<index>(indexes), rows.begin() };
}

template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
bool Table<Row, Indexes...>::eraseMatch(Params&&... params) {
  return eraseMatch<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
bool Table<Row, Indexes...>::eraseMatch(Params&&... params) {
  KJ_IF_MAYBE(pos, get<index>(indexes).find(rows.asPtr(), kj::fwd<Params>(params)...)) {
    eraseImpl(*pos);
    return true;
  } else {
    return false;
  }
}

template <typename Row, typename... Indexes>
template <typename Index, typename... Params>
size_t Table<Row, Indexes...>::eraseRange(Params&&... params) {
  return eraseRange<indexOfType<Index, Tuple<Indexes...>>()>(kj::fwd<Params>(params)...);
}
template <typename Row, typename... Indexes>
template <size_t index, typename... Params>
size_t Table<Row, Indexes...>::eraseRange(Params&&... params) {
  return eraseAllImpl(get<index>(indexes).range(rows.asPtr(), kj::fwd<Params>(params)...));
}

template <typename Row, typename... Indexes>
template <size_t index>
void Table<Row, Indexes...>::verify() {
  get<index>(indexes).verify(rows.asPtr());
}

template <typename Row, typename... Indexes>
void Table<Row, Indexes...>::erase(Row& row) {
  KJ_IREQUIRE(&row >= rows.begin() && &row < rows.end(), "row is not a member of this table");
  eraseImpl(&row - rows.begin());
}
template <typename Row, typename... Indexes>
void Table<Row, Indexes...>::eraseImpl(size_t pos) {
  Impl<>::erase(*this, pos);
  size_t back = rows.size() - 1;
  if (pos != back) {
    Impl<>::move(*this, back, pos);
    rows[pos] = kj::mv(rows[back]);
  }
  rows.removeLast();
}

template <typename Row, typename... Indexes>
template <typename Predicate, typename>
size_t Table<Row, Indexes...>::eraseAll(Predicate&& predicate) {
  size_t count = 0;
  for (size_t i = 0; i < rows.size();) {
    if (predicate(rows[i])) {
      eraseImpl(i);
      ++count;
      // eraseImpl() replaces the erased row with the last row, so don't increment i here; repeat
      // with the same i.
    } else {
      ++i;
    }
  }
  return count;
}

template <typename Row, typename... Indexes>
template <typename Collection, typename, bool>
size_t Table<Row, Indexes...>::eraseAll(Collection&& collection) {
  return eraseAllImpl(_::MappedIterable<Collection&, _::TableUnmapping<Row>>(
      collection, rows.begin()));
}

template <typename Row, typename... Indexes>
template <typename Collection>
size_t Table<Row, Indexes...>::eraseAllImpl(Collection&& collection) {
  // We need to transform the collection of row numbers into a sequence of erasures, accounting
  // for the fact that each erasure re-positions the last row into its slot.
  Vector<size_t> erased;
  _::tryReserveSize(erased, collection);
  for (size_t pos: collection) {
    while (pos >= rows.size() - erased.size()) {
      // Oops, the next item to be erased is already scheduled to be moved to a different location
      // due to a previous erasure. Figure out where it will be at this point.
      size_t erasureNumber = rows.size() - pos - 1;
      pos = erased[erasureNumber];
    }
    erased.add(pos);
  }

  // Now we can execute the sequence of erasures.
  for (size_t pos: erased) {
    eraseImpl(pos);
  }

  return erased.size();
}

// -----------------------------------------------------------------------------
// Hash table index

namespace _ {  // private

void logHashTableInconsistency();

struct HashBucket {
  uint hash;
  uint value;

  HashBucket() = default;
  HashBucket(uint hash, uint pos)
      : hash(hash), value(pos + 2) {}

  inline bool isEmpty() const { return value == 0; }
  inline bool isErased() const { return value == 1; }
  inline bool isOccupied() const { return value >= 2; }
  template <typename Row>
  inline Row& getRow(ArrayPtr<Row> table) const { return table[getPos()]; }
  template <typename Row>
  inline const Row& getRow(ArrayPtr<const Row> table) const { return table[getPos()]; }
  inline bool isPos(uint pos) const { return pos + 2 == value; }
  inline uint getPos() const {
    KJ_IASSERT(value >= 2);
    return value - 2;
  }
  inline void setEmpty() { value = 0; }
  inline void setErased() { value = 1; }
  inline void setPos(uint pos) { value = pos + 2; }
};

inline size_t probeHash(const kj::Array<HashBucket>& buckets, size_t i) {
  // TODO(perf): Is linear probing OK or should we do something fancier?
  if (++i == buckets.size()) {
    return 0;
  } else {
    return i;
  }
}

kj::Array<HashBucket> rehash(kj::ArrayPtr<const HashBucket> oldBuckets, size_t targetSize);

}  // namespace _ (private)

template <typename Callbacks>
class HashIndex {
public:
  HashIndex() = default;
  template <typename... Params>
  HashIndex(Params&&... params): cb(kj::fwd<Params>(params)...) {}

  void reserve(size_t size) {
    if (buckets.size() < size * 2) {
      rehash(size);
    }
  }

  void clear() {
    erasedCount = 0;
    memset(buckets.begin(), 0, buckets.asBytes().size());
  }

  template <typename Row>
  kj::Maybe<size_t> insert(kj::ArrayPtr<Row> table, size_t pos) {
    if (buckets.size() * 2 < (table.size() + erasedCount) * 3) {
      // Load factor is more than 2/3, let's rehash.
      rehash(kj::max(buckets.size() * 2, table.size() * 2));
    }

    uint hashCode = cb.hashCode(table[pos]);
    Maybe<_::HashBucket&> erasedSlot;
    for (uint i = hashCode % buckets.size();; i = _::probeHash(buckets, i)) {
      auto& bucket = buckets[i];
      if (bucket.isEmpty()) {
        // no duplicates found
        KJ_IF_MAYBE(s, erasedSlot) {
          --erasedCount;
          *s = { hashCode, uint(pos) };
        } else {
          bucket = { hashCode, uint(pos) };
        }
        return nullptr;
      } else if (bucket.isErased()) {
        // We can fill in the erased slot. However, we have to keep searching to make sure there
        // are no duplicates before we do that.
        if (erasedSlot == nullptr) {
          erasedSlot = bucket;
        }
      } else if (bucket.hash == hashCode &&
                 cb.matches(bucket.getRow(table), table[pos])) {
        // duplicate row
        return size_t(bucket.getPos());
      }
    }
  }

  template <typename Row>
  void erase(kj::ArrayPtr<Row> table, size_t pos) {
    uint hashCode = cb.hashCode(table[pos]);
    for (uint i = hashCode % buckets.size();; i = _::probeHash(buckets, i)) {
      auto& bucket = buckets[i];
      if (bucket.isPos(pos)) {
        // found it
        ++erasedCount;
        bucket.setErased();
        return;
      } else if (bucket.isEmpty()) {
        // can't find the bucket, something is very wrong
        _::logHashTableInconsistency();
        return;
      }
    }
  }

  template <typename Row>
  void move(kj::ArrayPtr<Row> table, size_t oldPos, size_t newPos) {
    uint hashCode = cb.hashCode(table[oldPos]);
    for (uint i = hashCode % buckets.size();; i = _::probeHash(buckets, i)) {
      auto& bucket = buckets[i];
      if (bucket.isPos(oldPos)) {
        // found it
        bucket.setPos(newPos);
        return;
      } else if (bucket.isEmpty()) {
        // can't find the bucket, something is very wrong
        _::logHashTableInconsistency();
        return;
      }
    }
  }

  template <typename Row, typename... Params>
  Maybe<size_t> find(kj::ArrayPtr<Row> table, Params&&... params) const {
    if (buckets.size() == 0) return nullptr;

    uint hashCode = cb.hashCode(params...);
    for (uint i = hashCode % buckets.size();; i = _::probeHash(buckets, i)) {
      auto& bucket = buckets[i];
      if (bucket.isEmpty()) {
        // not found.
        return nullptr;
      } else if (bucket.isErased()) {
        // skip, keep searching
      } else if (bucket.hash == hashCode &&
                 cb.matches(bucket.getRow(table), params...)) {
        // found
        return size_t(bucket.getPos());
      }
    }
  }

  // No begin() nor end() because hash tables are not usefully ordered.

private:
  Callbacks cb;
  size_t erasedCount = 0;
  Array<_::HashBucket> buckets;

  void rehash(size_t targetSize) {
    buckets = _::rehash(buckets, targetSize);
  }
};

// -----------------------------------------------------------------------------
// BTree index

namespace _ {  // private

KJ_ALWAYS_INLINE(void compilerBarrier()) {
  // Make sure that reads occurring before this call cannot be re-ordered to happen after
  // writes that occur after this call. We need this in a couple places below to prevent C++
  // strict aliasing rules from breaking things.
#if _MSC_VER
  _ReadWriteBarrier()
#else
  __asm__ __volatile__("": : :"memory");
#endif
}

template <typename T>
inline void acopy(T* to, T* from, size_t size) { memcpy(to, from, size * sizeof(T)); }
template <typename T>
inline void amove(T* to, T* from, size_t size) { memmove(to, from, size * sizeof(T)); }
template <typename T>
inline void azero(T* ptr, size_t size) { memset(ptr, 0, size * sizeof(T)); }
// memcpy/memmove/memset variants that count size in elements, not bytes.
//
// TODO(cleanup): These are generally useful, put them somewhere.

class BTreeImpl {
public:
  class Iterator;
  class MaybeUint;
  struct NodeUnion;
  struct Leaf;
  struct Parent;
  struct Freelisted;

  class SearchKey {
    // Passed to methods that need to search the tree. This class allows most of the B-tree
    // implementation to be kept out of templates, avoiding code bloat, at the cost of some
    // performance trade-off. In order to lessen the performance cost of virtual calls, we design
    // this interface so that it only needs to be called once per tree node, rather than once per
    // comparison.

  public:
    virtual uint search(const Parent& parent) const = 0;
    virtual uint search(const Leaf& leaf) const = 0;
    // Binary search for the first key/row in the parent/leaf that is equal to or comes after the
    // search key.

    virtual bool isAfter(uint rowIndex) const = 0;
    // Returns true if the key comes after the value in the given row.
  };

  BTreeImpl();
  ~BTreeImpl() noexcept(false);

  void logInconsitency() const;

  void reserve(size_t size);

  void clear();

  Iterator begin() const;
  Iterator end() const;

  Iterator search(const SearchKey& searchKey) const;
  // Find the "first" row number (in sorted order) for which predicate(rowNumber) returns false.

  Iterator insert(const SearchKey& searchKey);
  // Like search() but ensures that there is room in the leaf node to insert a new row.

  void erase(uint row, const SearchKey& searchKey);
  // Erase the given row number from the tree. predicate() returns false for the given row and all
  // rows after it.

  void renumber(uint oldRow, uint newRow, const SearchKey& searchKey);
  // Renumber the given row from oldRow to newRow. predicate() returns false for oldRow and all
  // rows after it. (It will not be called on newRow.)

  void verify(size_t size, FunctionParam<bool(uint, uint)>);

private:
  NodeUnion* tree;        // allocated with aligned_alloc aligned to cache lines
  uint treeCapacity;
  uint height;            // height of *parent* tree -- does not include the leaf level
  uint freelistHead;
  uint freelistSize;
  uint beginLeaf;
  uint endLeaf;
  void growTree(uint minCapacity = 0);

  template <typename T>
  struct AllocResult;

  template <typename T>
  inline AllocResult<T> alloc();
  inline void free(uint pos);

  inline uint split(Parent& src, uint srcPos, Parent& dst, uint dstPos);
  inline uint split(Leaf& dst, uint dstPos, Leaf& src, uint srcPos);
  inline void merge(Parent& dst, uint dstPos, uint pivot, Parent& src);
  inline void merge(Leaf& dst, uint dstPos, uint pivot, Leaf& src);
  inline void move(Parent& dst, uint dstPos, Parent& src);
  inline void move(Leaf& dst, uint dstPos, Leaf& src);
  inline void rotateLeft(
      Parent& left, Parent& right, Parent& parent, uint indexInParent, MaybeUint*& fixup);
  inline void rotateLeft(
      Leaf& left, Leaf& right, Parent& parent, uint indexInParent, MaybeUint*& fixup);
  inline void rotateRight(Parent& left, Parent& right, Parent& parent, uint indexInParent);
  inline void rotateRight(Leaf& left, Leaf& right, Parent& parent, uint indexInParent);

  template <typename Node>
  inline Node& insertHelper(const SearchKey& searchKey,
      Node& node, Parent* parent, uint indexInParent, uint pos);

  template <typename Node>
  inline Node& eraseHelper(
      Node& node, Parent* parent, uint indexInParent, uint pos, MaybeUint*& fixup);

  size_t verifyNode(size_t size, FunctionParam<bool(uint, uint)>&,
                    uint pos, uint height, MaybeUint maxRow);

  static const NodeUnion EMPTY_NODE;
};

class BTreeImpl::MaybeUint {
  // A nullable uint, using the value zero to mean null and shifting all other values up by 1.
public:
  MaybeUint() = default;
  inline MaybeUint(uint i): i(i - 1) {}
  inline MaybeUint(decltype(nullptr)): i(0) {}

  inline bool operator==(decltype(nullptr)) const { return i == 0; }
  inline bool operator==(uint j) const { return i == j + 1; }
  inline bool operator==(const MaybeUint& other) const { return i == other.i; }
  inline bool operator!=(decltype(nullptr)) const { return i != 0; }
  inline bool operator!=(uint j) const { return i != j + 1; }
  inline bool operator!=(const MaybeUint& other) const { return i != other.i; }

  inline MaybeUint& operator=(decltype(nullptr)) { i = 0; return *this; }
  inline MaybeUint& operator=(uint j) { i = j + 1; return *this; }

  inline uint operator*() const { KJ_IREQUIRE(i != 0); return i - 1; }

  template <typename Func>
  inline bool check(Func& func) const { return i != 0 && func(i - 1); }
  // Equivalent to *this != nullptr && func(**this)

private:
  uint i;
};

struct BTreeImpl::Leaf {
  uint next;
  uint prev;
  // Pointers to next and previous nodes at the same level, used for fast iteration.

  MaybeUint rows[14];
  // Pointers to table rows, offset by 1 so that 0 is an empty value.

  inline bool isFull() const;
  inline bool isMostlyFull() const;
  inline bool isHalfFull() const;

  inline void insert(uint i, uint newRow) {
    KJ_IREQUIRE(rows[kj::size(&Leaf::rows) - 1] == nullptr);  // check not full

    amove(rows + i + 1, rows + i, kj::size(&Leaf::rows) - (i + 1));
    rows[i] = newRow;
  }

  inline void erase(uint i) {
    KJ_IREQUIRE(rows[0] != nullptr);  // check not empty

    amove(rows + i, rows + i + 1, kj::size(&Leaf::rows) - (i + 1));
    rows[kj::size(&Leaf::rows) - 1] = nullptr;
  }

  inline uint size() const {
    static_assert(kj::size(&Leaf::rows) == 14, "logic here needs updating");

    // Binary search for first empty element in `rows`, or return 14 if no empty elements. We do
    // this in a branch-free manner. Since there are 15 possible results (0 through 14, inclusive),
    // this isn't a perfectly balanced binary search. We carefully choose the split points so that
    // there's no way we'll try to dereference row[14] or later (which would be a buffer overflow).
    uint i = (rows[6] != nullptr) * 7;
    i += (rows[i + 3] != nullptr) * 4;
    i += (rows[i + 1] != nullptr) * 2;
    i += (rows[i    ] != nullptr);
    return i;
  }

  template <typename Func>
  inline uint binarySearch(Func& predicate) const {
    // Binary search to find first row for which predicate(row) is false.

    static_assert(kj::size(&Leaf::rows) == 14, "logic here needs updating");

    // See comments in size().
    uint i = (rows[6].check(predicate)) * 7;
    i += (rows[i + 3].check(predicate)) * 4;
    i += (rows[i + 1].check(predicate)) * 2;
    if (i != 6) {  // don't redundantly check row 6
      i += (rows[i    ].check(predicate));
    }
    return i;
  }
};

struct BTreeImpl::Parent {
  uint unused;
  // Not used. May be arbitrarily non-zero due to overlap with Freelisted::nextOffset.

  MaybeUint keys[7];
  // Pointers to table rows, offset by 1 so that 0 is an empty value.

  uint children[kj::size(&Parent::keys) + 1];
  // Pointers to children. Not offset because the root is always at position 0, and a pointer
  // to the root would be nonsensical.

  inline bool isFull() const;
  inline bool isMostlyFull() const;
  inline bool isHalfFull() const;
  inline void initRoot(uint key, uint leftChild, uint rightChild);
  inline void insertAfter(uint i, uint splitKey, uint child);
  inline void eraseAfter(uint i);

  inline uint keyCount() const {
    static_assert(kj::size(&Parent::keys) == 7, "logic here needs updating");

    // Binary search for first empty element in `keys`, or return 7 if no empty elements. We do
    // this in a branch-free manner. Since there are 8 possible results (0 through 7, inclusive),
    // this is a perfectly balanced binary search.
    uint i = (keys[3] != nullptr) * 4;
    i += (keys[i + 1] != nullptr) * 2;
    i += (keys[i    ] != nullptr);
    return i;
  }

  template <typename Func>
  inline uint binarySearch(Func& predicate) const {
    // Binary search to find first key for which predicate(key) is false.

    static_assert(kj::size(&Parent::keys) == 7, "logic here needs updating");

    // See comments in size().
    uint i = (keys[3].check(predicate)) * 4;
    i += (keys[i + 1].check(predicate)) * 2;
    i += (keys[i    ].check(predicate));
    return i;
  }
};

struct BTreeImpl::Freelisted {
  int nextOffset;
  // The next node in the freelist is at: this + 1 + nextOffset
  //
  // Hence, newly-allocated space can initialize this to zero.

  uint zero[15];
  // Freelisted entries are always zero'd.
};

struct BTreeImpl::NodeUnion {
  union {
    Freelisted freelist;
    // If this node is in the freelist.

    Leaf leaf;
    // If this node is a leaf.

    Parent parent;
    // If this node is not a leaf.
  };

  inline operator Leaf&() { return leaf; }
  inline operator Parent&() { return parent; }
  inline operator const Leaf&() const { return leaf; }
  inline operator const Parent&() const { return parent; }
};

static_assert(sizeof(BTreeImpl::Parent) == 64,
    "BTreeImpl::Parent should be optimized to fit a cache line");
static_assert(sizeof(BTreeImpl::Leaf) == 64,
    "BTreeImpl::Leaf should be optimized to fit a cache line");
static_assert(sizeof(BTreeImpl::Freelisted) == 64,
    "BTreeImpl::Freelisted should be optimized to fit a cache line");
static_assert(sizeof(BTreeImpl::NodeUnion) == 64,
    "BTreeImpl::NodeUnion should be optimized to fit a cache line");

bool BTreeImpl::Leaf::isFull() const {
  return rows[kj::size(&Leaf::rows) - 1] != nullptr;
}
bool BTreeImpl::Leaf::isMostlyFull() const {
  return rows[kj::size(&Leaf::rows) / 2] != nullptr;
}
bool BTreeImpl::Leaf::isHalfFull() const {
  KJ_IASSERT(rows[kj::size(&Leaf::rows) / 2 - 1] != nullptr);
  return rows[kj::size(&Leaf::rows) / 2] == nullptr;
}

bool BTreeImpl::Parent::isFull() const {
  return keys[kj::size(&Parent::keys) - 1] != nullptr;
}
bool BTreeImpl::Parent::isMostlyFull() const {
  return keys[kj::size(&Parent::keys) / 2] != nullptr;
}
bool BTreeImpl::Parent::isHalfFull() const {
  KJ_IASSERT(keys[kj::size(&Parent::keys) / 2 - 1] != nullptr);
  return keys[kj::size(&Parent::keys) / 2] == nullptr;
}

class BTreeImpl::Iterator {
public:
  Iterator(const NodeUnion* tree, const Leaf* leaf, uint row)
      : tree(tree), leaf(leaf), row(row) {}

  size_t operator*() const {
    KJ_IREQUIRE(row < kj::size(&Leaf::rows) && leaf->rows[row] != nullptr,
        "tried to dereference end() iterator");
    return *leaf->rows[row];
  }

  inline Iterator& operator++() {
    KJ_IREQUIRE(leaf->rows[row] != nullptr, "B-tree iterator overflow");
    ++row;
    if (row >= kj::size(&Leaf::rows) || leaf->rows[row] == nullptr) {
      if (leaf->next == 0) {
        // at end; stay on current leaf
      } else {
        leaf = &tree[leaf->next].leaf;
        row = 0;
      }
    }
    return *this;
  }
  inline Iterator operator++(int) {
    Iterator other = *this;
    ++*this;
    return other;
  }

  inline Iterator& operator--() {
    if (row == 0) {
      KJ_IREQUIRE(leaf->prev != 0, "B-tree iterator underflow");
      leaf = &tree[leaf->prev].leaf;
      row = leaf->size() - 1;
    } else {
      --row;
    }
    return *this;
  }
  inline Iterator operator--(int) {
    Iterator other = *this;
    --*this;
    return other;
  }

  inline bool operator==(const Iterator& other) const {
    return leaf == other.leaf && row == other.row;
  }
  inline bool operator!=(const Iterator& other) const {
    return leaf != other.leaf || row != other.row;
  }

  bool isEnd() {
    return row == kj::size(&Leaf::rows) || leaf->rows[row] == nullptr;
  }

  void insert(BTreeImpl& impl, uint newRow) {
    KJ_IASSERT(impl.tree == tree);
    const_cast<Leaf*>(leaf)->insert(row, newRow);
  }

  void erase(BTreeImpl& impl) {
    KJ_IASSERT(impl.tree == tree);
    const_cast<Leaf*>(leaf)->erase(row);
  }

  void replace(BTreeImpl& impl, uint newRow) {
    KJ_IASSERT(impl.tree == tree);
    const_cast<Leaf*>(leaf)->rows[row] = newRow;
  }

private:
  const NodeUnion* tree;
  const Leaf* leaf;
  uint row;
};

template <typename Iterator>
class IterRange {
public:
  inline IterRange(Iterator b, Iterator e): b(b), e(e) {}

  inline Iterator begin() const { return b; }
  inline Iterator end() const { return e; }
private:
  Iterator b;
  Iterator e;
};

template <typename Iterator>
inline IterRange<Decay<Iterator>> iterRange(Iterator b, Iterator e) {
  return { b, e };
}

inline BTreeImpl::Iterator BTreeImpl::begin() const {
  return { tree, &tree[beginLeaf].leaf, 0 };
}
inline BTreeImpl::Iterator BTreeImpl::end() const {
  auto& leaf = tree[endLeaf].leaf;
  return { tree, &leaf, leaf.size() };
}

}  // namespace _ (private)

template <typename Callbacks>
class TreeIndex {
public:
  TreeIndex() = default;
  template <typename... Params>
  TreeIndex(Params&&... params): cb(kj::fwd<Params>(params)...) {}

  template <typename Row>
  void verify(kj::ArrayPtr<Row> table) { // KJ_DBG
    impl.verify(table.size(), [&](uint i, uint j) {
      return cb.isBefore(table[i], table[j]);
    });
  }

  inline void reserve(size_t size) { impl.reserve(size); }
  inline void clear() { impl.clear(); }
  inline auto begin() const { return impl.begin(); }
  inline auto end() const { return impl.end(); }

  template <typename Row>
  kj::Maybe<size_t> insert(kj::ArrayPtr<Row> table, size_t pos) {
    auto& newRow = table[pos];
    auto iter = impl.insert(searchKey(table, newRow));

    if (!iter.isEnd() && cb.matches(table[*iter], newRow)) {
      return *iter;
    } else {
      iter.insert(impl, pos);
      return nullptr;
    }
  }

  template <typename Row>
  void erase(kj::ArrayPtr<Row> table, size_t pos) {
    auto& row = table[pos];
    impl.erase(pos, searchKey(table, row));
  }

  template <typename Row>
  void move(kj::ArrayPtr<Row> table, size_t oldPos, size_t newPos) {
    auto& row = table[oldPos];
    impl.renumber(oldPos, newPos, searchKey(table, row));
  }

  template <typename Row, typename... Params>
  Maybe<size_t> find(kj::ArrayPtr<Row> table, Params&&... params) const {
    auto iter = impl.search(searchKey(table, params...));

    if (!iter.isEnd() && cb.matches(table[*iter], params...)) {
      return size_t(*iter);
    } else {
      return nullptr;
    }
  }

  template <typename Row, typename Begin, typename End>
  _::IterRange<_::BTreeImpl::Iterator> range(
      kj::ArrayPtr<Row> table, Begin&& begin, End&& end) const {
    return {
      impl.search(searchKey(table, begin)),
      impl.search(searchKey(table, end  ))
    };
  }

private:
  Callbacks cb;
  _::BTreeImpl impl;

  template <typename Predicate>
  class SearchKeyImpl: public _::BTreeImpl::SearchKey {
  public:
    SearchKeyImpl(Predicate&& predicate)
        : predicate(kj::mv(predicate)) {}

    uint search(const _::BTreeImpl::Parent& parent) const override {
      return parent.binarySearch(predicate);
    }
    uint search(const _::BTreeImpl::Leaf& leaf) const override {
      return leaf.binarySearch(predicate);
    }
    bool isAfter(uint rowIndex) const override {
      return predicate(rowIndex);
    }

  private:
    Predicate predicate;
  };

  template <typename Row, typename... Params>
  inline auto searchKey(kj::ArrayPtr<Row>& table, Params&... params) const {
    auto predicate = [&](uint i) { return cb.isBefore(table[i], params...); };
    return SearchKeyImpl<decltype(predicate)>(kj::mv(predicate));
  }
};

// -----------------------------------------------------------------------------
// Insertion order index

class InsertionOrderIndex {
  // Table index which allows iterating over elements in order of insertion. This index cannot
  // be used for Table::find(), but can be used for Table::ordered().

  struct Link;
public:
  InsertionOrderIndex();
  ~InsertionOrderIndex() noexcept(false);

  class Iterator {
  public:
    Iterator(const Link* links, uint pos)
        : links(links), pos(pos) {}

    inline size_t operator*() const {
      KJ_IREQUIRE(pos != 0, "can't derefrence end() iterator");
      return pos - 1;
    };

    inline Iterator& operator++() {
      pos = links[pos].next;
      return *this;
    }
    inline Iterator operator++(int) {
      Iterator result = *this;
      ++*this;
      return result;
    }
    inline Iterator& operator--() {
      pos = links[pos].prev;
      return *this;
    }
    inline Iterator operator--(int) {
      Iterator result = *this;
      --*this;
      return result;
    }

    inline bool operator==(const Iterator& other) const {
      return pos == other.pos;
    }
    inline bool operator!=(const Iterator& other) const {
      return pos != other.pos;
    }

  private:
    const Link* links;
    uint pos;
  };

  void reserve(size_t size);
  void clear();
  inline Iterator begin() const { return Iterator(links, links[0].next); }
  inline Iterator end() const { return Iterator(links, 0); }

  template <typename Row>
  kj::Maybe<size_t> insert(kj::ArrayPtr<Row> table, size_t pos) {
    return insertImpl(pos);
  }

  template <typename Row>
  void erase(kj::ArrayPtr<Row> table, size_t pos) {
    eraseImpl(pos);
  }

  template <typename Row>
  void move(kj::ArrayPtr<Row> table, size_t oldPos, size_t newPos) {
    return moveImpl(oldPos, newPos);
  }

private:
  struct Link {
    uint next;
    uint prev;
  };

  uint capacity;
  Link* links;
  // links[0] is special: links[0].next points to the first link, links[0].prev points to the last.
  // links[n+1] corresponds no row n.

  kj::Maybe<size_t> insertImpl(size_t pos);
  void eraseImpl(size_t pos);
  void moveImpl(size_t oldPos, size_t newPos);

  static const Link EMPTY_LINK;
};

} // namespace kj

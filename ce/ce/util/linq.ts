// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

export interface Dictionary<T> {
  [key: string]: T;
}

export class Dictionary<T> implements Dictionary<T> {
}

export function ToDictionary<T>(keys: Array<string>, each: (index: string) => T) {
  const result = new Dictionary<T>();
  keys.map((v, i, a) => result[v] = each(v));
  return result;
}

export type IndexOf<T> = T extends Map<T, infer V> ? T : T extends Array<infer V> ? number : string;

/** performs a truthy check on the value, and calls onTrue when the condition is true,and onFalse when it's not */
export function when<T>(value: T, onTrue: (value: NonNullable<T>) => void, onFalse: () => void = () => { /* */ }) {
  return value ? onTrue(<NonNullable<T>>value) : onFalse();
}

export interface IterableWithLinq<T> extends Iterable<T> {
  linq: IterableWithLinq<T>;
  any(predicate?: (each: T) => boolean): boolean;
  all(predicate: (each: T) => boolean): boolean;
  bifurcate(predicate: (each: T) => boolean): Array<Array<T>>;
  concat(more: Iterable<T>): IterableWithLinq<T>;
  distinct(selector?: (each: T) => any): IterableWithLinq<T>;
  duplicates(selector?: (each: T) => any): IterableWithLinq<T>;
  first(predicate?: (each: T) => boolean): T | undefined;
  selectNonNullable<V>(selector: (each: T) => V): IterableWithLinq<NonNullable<V>>;
  select<V>(selector: (each: T) => V): IterableWithLinq<V>;
  selectMany<V>(selector: (each: T) => Iterable<V>): IterableWithLinq<V>;
  where(predicate: (each: T) => boolean): IterableWithLinq<T>;
  forEach(action: (each: T) => void): void;
  aggregate<A, R>(accumulator: (current: T | A, next: T) => A, seed?: T | A, resultAction?: (result?: T | A) => A | R): T | A | R | undefined;
  toArray(): Array<T>;
  toObject<V, U>(selector: (each: T) => [V, U]): Record<string, U>;
  results(): Promise<void>;
  toDictionary<TValue>(keySelector: (each: T) => string, selector: (each: T) => TValue): Dictionary<TValue>;
  toMap<TKey, TValue>(keySelector: (each: T) => TKey, selector: (each: T) => TValue): Map<TKey, TValue>;
  groupBy<TKey, TValue>(keySelector: (each: T) => TKey, selector: (each: T) => TValue): Map<TKey, Array<TValue>>;

  /**
     * Gets or sets the length of the iterable. This is a number one higher than the highest element defined in an array.
     */
  count(): number;

  /**
    * Adds all the elements of an array separated by the specified separator string.
    * @param separator A string used to separate one element of an array from the next in the resulting String. If omitted, the array elements are separated with a comma.
    */
  join(separator?: string): string;

}

/* eslint-disable */

function linqify<T>(iterable: Iterable<T> | IterableIterator<T>): IterableWithLinq<T> {
  if ((<any>iterable)['linq'] === iterable) {
    return <IterableWithLinq<T>>iterable;
  }
  const r = <any>{
    [Symbol.iterator]: iterable[Symbol.iterator].bind(iterable),
    all: <any>all.bind(iterable),
    any: <any>any.bind(iterable),
    bifurcate: <any>bifurcate.bind(iterable),
    concat: <any>concat.bind(iterable),
    distinct: <any>distinct.bind(iterable),
    duplicates: <any>duplicates.bind(iterable),
    first: <any>first.bind(iterable),
    select: <any>select.bind(iterable),
    selectMany: <any>selectMany.bind(iterable),
    selectNonNullable: <any>selectNonNullable.bind(iterable),
    toArray: <any>toArray.bind(iterable),
    toObject: <any>toObject.bind(iterable),
    where: <any>where.bind(iterable),
    forEach: <any>forEach.bind(iterable),
    aggregate: <any>aggregate.bind(iterable),
    join: <any>join.bind(iterable),
    count: len.bind(iterable),
    results: <any>results.bind(iterable),
    toDictionary: <any>toDictionary.bind(iterable),
    toMap: <any>toMap.bind(iterable),
    groupBy: <any>groupBy.bind(iterable),
  };
  r.linq = r;
  return r;
}

function len<T>(this: Iterable<T>): number {
  return length(this);
}

export function keys<K, T>(source: Map<K, T> | null | undefined): Iterable<K>
export function keys<T, TSrc extends Dictionary<T>>(source: Dictionary<T> | null | undefined): Iterable<string>
export function keys<T, TSrc extends Array<T>>(source: Array<T> | null | undefined): Iterable<number>
export function keys<K, T, TSrc>(source: any | undefined | null): Iterable<any>
export function keys<K, T, TSrc>(source: any): Iterable<any> {
  if (source) {
    if (Array.isArray(source)) {
      return <Iterable<IndexOf<TSrc>>>(<Array<T>>source).keys();
    }

    if (source instanceof Map) {
      return <Iterable<K>><unknown>(<Map<K, T>>source).keys();
    }

    if (source instanceof Set) {
      throw new Error('Unable to iterate keys on a Set');
    }

    return <Iterable<IndexOf<TSrc>>>Object.keys(source);
  }
  // undefined/null
  return [];
}



/** returns an IterableWithLinq<> for keys in the collection */
function _keys<K, T>(source: Map<K, T> | null | undefined): IterableWithLinq<K>
function _keys<T, TSrc extends Dictionary<T>>(source: Dictionary<T> | null | undefined): IterableWithLinq<string>
function _keys<T, TSrc extends Array<T>>(source: Array<T> | null | undefined): IterableWithLinq<number>
function _keys<K, T, TSrc>(source: any | undefined | null): IterableWithLinq<any>
function _keys<K, T, TSrc>(source: any): IterableWithLinq<any> {
  //export function keys<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T>)>(source: TSrc & (Array<T> | Dictionary<T> | Map<K, T>) | null | undefined): IterableWithLinq<IndexOf<TSrc>> {
  if (source) {
    if (Array.isArray(source)) {
      return <IterableWithLinq<IndexOf<TSrc>>>linqify((<Array<T>>source).keys());
    }

    if (source instanceof Map) {
      return <IterableWithLinq<K>><unknown>linqify((<Map<K, T>>source).keys());
    }

    if (source instanceof Set) {
      throw new Error('Unable to iterate keys on a Set');
    }

    return <IterableWithLinq<IndexOf<TSrc>>>linqify((Object.keys(source)));
  }
  // undefined/null
  return linqify([]);
}
function isIterable<T>(source: any): source is Iterable<T> {
  return !!source && !!source[Symbol.iterator];
}

export function values<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T>)>(source: (Iterable<T> | Array<T> | Dictionary<T> | Map<K, T> | Set<T>) | null | undefined): Iterable<T> {
  if (source) {
    // map
    if (source instanceof Map || source instanceof Set) {
      return source.values();
    }

    // any iterable source
    if (isIterable(source)) {
      return source;
    }

    // dictionary (object keys)
    return Object.values(source);
  }

  // null/undefined
  return [];
}
export const linq = {
  values: _values,
  entries: _entries,
  keys: _keys,
  find: _find,
  startsWith: _startsWith,
};

/** returns an IterableWithLinq<> for values in the collection
 *
 * @note - null/undefined/empty values are considered 'empty'
*/
function _values<K, T>(source: (Array<T> | Dictionary<T> | Map<K, T> | Set<T> | Iterable<T>) | null | undefined): IterableWithLinq<T> {
  return (source) ? linqify(values(source)) : linqify([]);
}

export function entries<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T> | undefined | null)>(source: TSrc & (Array<T> | Dictionary<T> | Map<K, T>) | null | undefined): Iterable<[IndexOf<TSrc>, T]> {
  if (source) {
    if (Array.isArray(source)) {
      return <Iterable<[IndexOf<TSrc>, T]>><any>source.entries();
    }

    if (source instanceof Map) {
      return <Iterable<[IndexOf<TSrc>, T]>><any><any>source.entries();
    }

    if (source instanceof Set) {
      throw new Error('Unable to iterate items on a Set (use values)');
    }

    return <Iterable<[IndexOf<TSrc>, T]>><unknown>Object.entries(source);
  }
  // undefined/null
  return [];
}

/** returns an IterableWithLinq<{key,value}> for the source */
function _entries<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T> | undefined | null)>(source: TSrc & (Array<T> | Dictionary<T> | Map<K, T>) | null | undefined): IterableWithLinq<[IndexOf<TSrc>, T]> {
  return <any>linqify(source ? entries(<any>source) : [])
}

/** returns the first value where the key equals the match value (case-insensitive) */
function _find<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T> | undefined | null)>(source: TSrc & (Array<T> | Dictionary<T> | Map<K, T>) | null | undefined, match: string): T | undefined {
  return _entries(source).first(([key,]) => key.toString().localeCompare(match, undefined, { sensitivity: 'base' }) === 0)?.[1];
}

/** returns the first value where the key starts with the match value (case-insensitive) */
function _startsWith<K, T, TSrc extends (Array<T> | Dictionary<T> | Map<K, T> | undefined | null)>(source: TSrc & (Array<T> | Dictionary<T> | Map<K, T>) | null | undefined, match: string): T | undefined {
  match = match.toLowerCase();
  return _entries(source).first(([key,]) => key.toString().toLowerCase().startsWith(match))?.[1];
}


export function length<T, K>(source?: string | Iterable<T> | Dictionary<T> | Array<T> | Map<K, T> | Set<T>): number {
  if (source) {
    if (Array.isArray(source) || typeof (source) === 'string') {
      return source.length;
    }
    if (source instanceof Map || source instanceof Set) {
      return source.size;
    }
    if (isIterable(source)) {
      return [...source].length;
    }
    return source ? Object.values(source).length : 0;
  }
  return 0;
}

function toDictionary<TElement, TValue>(this: Iterable<TElement>, keySelector: (each: TElement) => string, selector: (each: TElement) => TValue): Dictionary<TValue> {
  const result = new Dictionary<TValue>();
  for (const each of this) {
    result[keySelector(each)] = selector(each);
  }
  return result;
}

function toMap<TElement, TValue, TKey>(this: Iterable<TElement>, keySelector: (each: TElement) => TKey, selector: (each: TElement) => TValue): Map<TKey, TValue> {
  const result = new Map<TKey, TValue>();
  for (const each of this) {
    result.set(keySelector(each), selector(each));
  }
  return result;
}

function groupBy<TElement, TValue, TKey>(this: Iterable<TElement>, keySelector: (each: TElement) => TKey, selector: (each: TElement) => TValue): Map<TKey, TValue[]> {
  const result = new ManyMap<TKey, TValue>();
  for (const each of this) {
    result.push(keySelector(each), selector(each));
  }
  return result;
}

function any<T>(this: Iterable<T>, predicate?: (each: T) => boolean): boolean {
  for (const each of this) {
    if (!predicate || predicate(each)) {
      return true;
    }
  }
  return false;
}

function all<T>(this: Iterable<T>, predicate: (each: T) => boolean): boolean {
  for (const each of this) {
    if (!predicate(each)) {
      return false;
    }
  }
  return true;
}

function concat<T>(this: Iterable<T>, more: Iterable<T>): IterableWithLinq<T> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      yield each;
    }
    for (const each of more) {
      yield each;
    }
  }.bind(this)());
}

function select<T, V>(this: Iterable<T>, selector: (each: T) => V): IterableWithLinq<V> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      yield selector(each);
    }
  }.bind(this)());
}

function selectMany<T, V>(this: Iterable<T>, selector: (each: T) => Iterable<V>): IterableWithLinq<V> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      yield* selector(each);
    }
  }.bind(this)());
}

function where<T>(this: Iterable<T>, predicate: (each: T) => boolean): IterableWithLinq<T> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      if (predicate(each)) {
        yield each;
      }
    }
  }.bind(this)());
}

function forEach<T>(this: Iterable<T>, action: (each: T) => void) {
  for (const each of this) {
    action(each);
  }
}

function aggregate<T, A, R>(this: Iterable<T>, accumulator: (current: T | A, next: T) => A, seed?: T | A, resultAction?: (result?: T | A) => A | R): T | A | R | undefined {
  let result: T | A | undefined = seed;
  for (const each of this) {
    if (result === undefined) {
      result = each;
      continue;
    }
    result = accumulator(result, each);
  }
  return resultAction !== undefined ? resultAction(result) : result;
}

function selectNonNullable<T, V>(this: Iterable<T>, selector: (each: T) => V): IterableWithLinq<NonNullable<V>> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      const value = selector(each);
      if (value) {
        yield <NonNullable<V>><any>value;
      }
    }
  }.bind(this)());
}

function nonNullable<T>(this: Iterable<T>): IterableWithLinq<NonNullable<T>> {
  return linqify(function* (this: Iterable<T>) {
    for (const each of this) {
      if (each) {
        yield <NonNullable<T>><any>each;
      }
    }
  }.bind(this)());
}

function first<T>(this: Iterable<T>, predicate?: (each: T) => boolean): T | undefined {
  for (const each of this) {
    if (!predicate || predicate(each)) {
      return each;
    }
  }
  return undefined;
}

function toArray<T>(this: Iterable<T>): Array<T> {
  return [...this];
}

function toObject<T, V>(this: Iterable<T>, selector: (each: T) => [string, V]): Record<string, V> {
  const result = <Record<string, V>>{};
  for (const each of this) {
    const [key, value] = selector(each);
    result[key] = value;
  }
  return result;
}

async function results<T>(this: Iterable<T>): Promise<void> {
  await Promise.all([...<any>this]);
}


function join<T>(this: Iterable<T>, separator: string): string {
  return [...this].join(separator);
}

function bifurcate<T>(this: Iterable<T>, predicate: (each: T) => boolean): Array<Array<T>> {
  const result = [new Array<T>(), new Array<T>()];
  for (const each of this) {
    result[predicate(each) ? 0 : 1].push(each);
  }
  return result;
}

function distinct<T>(this: Iterable<T>, selector?: (each: T) => any): IterableWithLinq<T> {
  const hash = new Dictionary<boolean>();
  return linqify(function* (this: Iterable<T>) {

    if (!selector) {
      selector = i => i;
    }
    for (const each of this) {
      const k = JSON.stringify(selector(each));
      if (!hash[k]) {
        hash[k] = true;
        yield each;
      }
    }
  }.bind(this)());
}

function duplicates<T>(this: Iterable<T>, selector?: (each: T) => any): IterableWithLinq<T> {
  const hash = new Dictionary<boolean>();
  return linqify(function* (this: Iterable<T>) {

    if (!selector) {
      selector = i => i;
    }
    for (const each of this) {
      const k = JSON.stringify(selector(each));
      if (hash[k] === undefined) {
        hash[k] = false;
      } else {
        if (hash[k] === false) {
          hash[k] = true;
          yield each;
        }
      }
    }
  }.bind(this)());
}

/** A Map of Key: Array<Value>  */
export class ManyMap<K, V> extends Map<K, Array<V>> {
  /**
   * Push the value into the array at key
   * @param key the unique key in the map
   * @param value the value to push to the collection at 'key'
   */
  push(key: K, value: V) {
    this.getOrDefault(key, []).push(value);
  }
}

export function countWhere<T>(from: Iterable<T>, predicate: (each: T) => Promise<boolean>): Promise<number>
export function countWhere<T>(from: Iterable<T>, predicate: (each: T) => boolean): number
export function countWhere<T>(from: Iterable<T>, predicate: (e: T) => boolean | Promise<boolean>) {
  let v = 0;
  const all = [];
  for (const each of from) {
    const test = <any>predicate(each);
    if (test.then) {
      all.push(test.then((antecedent: any) => {
        if (antecedent) {
          v++;
        }
      }));
      continue;
    }
    if (test) {
      v++;
    }
  }
  if (all.length) {
    return Promise.all(all).then(() => v);
  }
  return v;
}
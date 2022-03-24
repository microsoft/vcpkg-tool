// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { Range, SemVer } from 'semver';
import BTree from 'sorted-btree';
import { isIterable } from '../util/checks';
import { intersect } from '../util/intersect';
import { Dictionary, entries, keys, ManyMap } from '../util/linq';

/* eslint-disable @typescript-eslint/ban-types */

/** Keys have to support toString so that we can serialize them */
interface HasToString {
  toString(): string;
}

/**
 * An Index is the means to search a registry
 *
 * @param TGraph The type of object to create an index for
 * @param TIndexSchema the custom index schema (layout).
 */
export class Index<TGraph extends Object, TIndexSchema extends IndexSchema<TGraph, TIndexSchema>> {
  /** @internal */
  indexSchema: TIndexSchema;
  /** @internal */
  indexOfTargets = new Array<string>();

  /**
   * Creates an index for fast searching.
   *
   * @param indexConstructor the class for the custom index.
   */
  constructor(protected indexConstructor: new (index: Index<TGraph, TIndexSchema>) => TIndexSchema) {
    this.indexSchema = new indexConstructor(this);
  }

  reset() {
    this.indexSchema = new this.indexConstructor(this);
  }

  /**
   * Serializes the index to a javascript object graph that can be persisted.
   */
  serialize() {
    return {
      items: this.indexOfTargets,
      indexes: this.indexSchema.serialize()
    };
  }

  /**
   * Deserializes an object graph to the expected indexes.
   *
   * @param content the object graph to deserialize.
   */
  deserialize(content: any) {
    this.indexOfTargets = content.items;
    this.indexSchema.deserialize(content.indexes);
  }

  /**
   * Returns a clone of the index that can be searched, which narrows the list of
   */
  get where(): TIndexSchema {
    // clone the index so that the consumer can filter on it.
    const index = new Index(this.indexConstructor);
    index.indexOfTargets = this.indexOfTargets;
    for (const [key, impl] of this.indexSchema.mapOfKeyObjects.entries()) {
      index.indexSchema.mapOfKeyObjects.get(key)!.cloneKey(impl);
    }
    return index.indexSchema;
  }

  /** inserts an object into the index */
  insert(content: TGraph, target: string) {
    const n = this.indexOfTargets.push(target) - 1;
    const start = process.uptime() * 1000;
    for (const indexKey of this.indexSchema.mapOfKeyObjects.values()) {
      indexKey.insert(content, n);
    }
  }

  doneInsertion() {
    for (const indexKey of this.indexSchema.mapOfKeyObjects.values()) {
      indexKey.doneInsertion();
    }
  }
}

/** deconstructs a function declaration so that we can figure out the most logical path  */
function deconstruct(accessor: string) {
  const params = /\(?([^(=)]+)\)?=>/g.exec(accessor);
  let names = ['i'];
  if (params) {
    names = params[1].split(',').map(each => each.trim());
  }

  // find the part that looks for the value
  let path = names.slice(1);
  const expression = /=>.*i\.(.*?)(;| |\n|$)/g.exec(accessor);
  if (expression) {
    path = expression[1].replace(/\[.*?\]/g, '').replace(/[^\w.]/g, '').replace(/\.+/g, '.').split('.');
  }

  strict.ok(path.length, 'Unable to deconstruct the path to the element');

  return path;
}

/**
 * A Key is a means to creating a searchable, sortable index
 */
abstract class Key<TGraph extends Object, TKey extends HasToString, TIndexSchema extends IndexSchema<TGraph, any>> {

  /** child class must implement a standard compare function */
  abstract compare(a: TKey, b: TKey): number;

  /** child class must implement a function to transform value into comparable key */
  abstract coerce(value: TKey | string): TKey;

  protected nestedKeys = new Array<Key<TGraph, any, TIndexSchema>>();
  protected values = new BTree<TKey, Set<number>>(undefined, this.compare);
  protected words = new BTree<string, Set<number>>();
  protected indexSchema: TIndexSchema;
  protected path: Array<string>;

  /** attaches a nested key in the index. */
  with<TNestedKey extends Dictionary<Key<TGraph, any, TIndexSchema>>>(nestedKey: TNestedKey): Key<TGraph, TKey, TIndexSchema> & TNestedKey {
    for (const child of keys(nestedKey)) {
      this.nestedKeys.push(nestedKey[child]);
    }
    return intersect(this, nestedKey);
  }

  /** returns the textual 'identity' of this key */
  get identity() {
    return `${this.constructor.name}/${this.path.join('.')}`;
  }

  /** persists the key to an object graph */
  serialize() {
    const result = <any>{
      keys: {},
      words: {},
    };
    for (const each of this.values.entries()) {
      result.keys[each[0]] = [...each[1]];
    }
    for (const each of this.words.entries()) {
      result.words[each[0]] = [...each[1]];
    }
    return result;
  }

  /** deserializes an object graph back into this key */
  deserialize(content: any) {
    for (const [key, ids] of entries(content.keys)) {
      this.values.set(this.coerce(key), new Set(<any>ids));
    }
    for (const [key, ids] of entries(content.words)) {
      this.words.set(key, new Set(<any>ids));
    }
  }

  /** @internal */
  cloneKey(from: this) {
    this.values = from.values.greedyClone();
    this.words = from.words.greedyClone();
  }

  /** adds key value to this Key */
  protected addKey(each: TKey, n: number) {
    let set = this.values.get(each);
    if (!set) {
      set = new Set<number>();
      this.values.set(each, set);
    }
    set.add(n);
  }

  /** adds a 'word' value to this key  */
  protected addWord(each: TKey, n: number) {
    const words = each.toString().split(/(\W+)/g);

    for (let word = 0; word < words.length; word += 2) {
      for (let i = word; i < words.length; i += 2) {
        const s = words.slice(word, i + 1).join('');
        if (s && s.indexOf(' ') === -1) {
          let set = this.words.get(s);
          if (!set) {
            set = new Set<number>();
            this.words.set(s, set);
          }
          set.add(n);
        }
      }
    }

  }

  /** processes an object to generate key/word values for it. */
  insert(graph: TGraph, n: number) {
    let value = this.accessor(graph);
    if (value) {
      value = <Array<TKey>>(Array.isArray(value) ? value
        : typeof value === 'string' ? [value]
          : isIterable(value) ? [...value] : [value]);

      this.insertKey(graph, n, value);
    }
  }

  /** insert the key/word values and process any children */
  private insertKey(graph: TGraph, n: number, value: TKey | Iterable<TKey>) {
    if (isIterable(value)) {
      for (const each of value) {
        this.addKey(each, n);
        this.addWord(each, n);
        if (this.nestedKeys) {
          for (const child of this.nestedKeys) {
            const v = child.accessor(graph, each.toString());
            if (v) {
              child.insertKey(graph, n, v);
            }
          }
        }
      }
    } else {
      this.addKey(value, n);
      this.addWord(value, n);
    }
  }

  /** construct a Key */
  constructor(indexSchema: IndexSchema<TGraph, TIndexSchema>, public accessor: (value: TGraph, ...args: Array<any>) => TKey | undefined | Array<TKey> | Iterable<TKey>) {
    this.path = deconstruct(accessor.toString());
    this.indexSchema = <TIndexSchema><unknown>indexSchema;
    this.indexSchema.mapOfKeyObjects.set(this.identity, this);
  }

  /** word search */
  contains(value: TKey | string): TIndexSchema {
    if (value !== undefined && value !== '') {
      const matches = this.words.get(value.toString());
      this.indexSchema.filter(matches || []);
    }
    return this.indexSchema;
  }

  /** exact match search */
  equals(value: TKey | string): TIndexSchema {
    if (value !== undefined && value !== '') {
      const matches = this.values.get(this.coerce(value));
      this.indexSchema.filter(matches || []);
    }
    return this.indexSchema;
  }

  /** metadata value is greater than search */
  greaterThan(value: TKey | string): TIndexSchema {
    const max = this.values.maxKey();
    const set = new Set<number>();
    if (max && value !== undefined && value !== '') {
      this.values.forRange(this.coerce(value), max, true, (k, v) => {
        for (const n of v) {
          set.add(n);
        }
      });
    }
    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }

  /** metadata value is less than search */
  lessThan(value: TKey | string): TIndexSchema {
    const min = this.values.minKey();
    const set = new Set<number>();
    if (min && value !== undefined && value !== '') {
      value = this.coerce(value);
      this.values.forRange(min, this.coerce(value), false, (k, v) => {
        for (const n of v) {
          set.add(n);
        }
      });
    }
    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }

  /** regex search -- WARNING: slower */
  match(regex: string): TIndexSchema {
    // This could be faster if we stored a reverse lookup
    // array that had the id for each key, but .. I don't
    // think the perf will suffer much doing it this way.

    const set = new Set<number>();

    for (const node of this.values.entries()) {
      for (const id of node[1]) {
        if (!this.indexSchema.selectedElements || this.indexSchema.selectedElements.has(id)) {
          // it's currently in the keep list.
          if (regex.match(node.toString())) {
            set.add(id);
          }
        }
      }
    }

    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }
  /** substring match -- slower */
  startsWith(value: TKey | string): TIndexSchema {
    // ok, I'm being lazy here. I can add a check to see if we're past
    // the point where this could be a match, but I don't know if I'll
    // even need this enough to keep it.

    const set = new Set<number>();

    for (const node of this.values.entries()) {
      for (const id of node[1]) {
        if (!this.indexSchema.selectedElements || this.indexSchema.selectedElements.has(id)) {
          // it's currently in the keep list.
          if (node[0].toString().startsWith((<any>value).toString())) {
            set.add(id);
          }
        }
      }
    }

    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }
  /** substring match -- slower */
  endsWith(value: TKey | string): TIndexSchema {
    // Same thing here, but I'd have to do a reversal of all the strings.

    const set = new Set<number>();

    for (const node of this.values.entries()) {
      for (const id of node[1]) {
        if (!this.indexSchema.selectedElements || this.indexSchema.selectedElements.has(id)) {
          // it's currently in the keep list.
          if (node[0].toString().endsWith((<any>value).toString())) {
            set.add(id);
          }
        }
      }
    }

    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }

  doneInsertion() {
    // nothing normally
  }
}

/** An  key for string values. */
export class StringKey<TGraph extends Object, TIndexSchema extends IndexSchema<TGraph, any>> extends Key<TGraph, string, TIndexSchema> {

  compare(a: string, b: string): number {
    if (a && b) {
      return a.localeCompare(b);
    }
    if (a) {
      return 1;
    }
    if (b) {
      return -1;
    }
    return 0;
  }

  /** impl: transform value into comparable key */
  coerce(value: string): string {
    return value;
  }
}

function shortName(value: string, n: number) {
  const v = value.split('/');
  let p = v.length - n;
  if (p < 0) {
    p = 0;
  }
  return v.slice(p).join('/');
}

export class IdentityKey<TGraph extends Object, TIndexSchema extends IndexSchema<TGraph, any>> extends StringKey<TGraph, TIndexSchema> {

  protected identities = new BTree<string, Set<number>>(undefined, this.compare);
  protected idShortName = new Map<string, string>();

  override doneInsertion() {
    // go thru each of the values, find short name for each.
    const ids = new ManyMap<string, [string, Set<number>]>();

    for (const idAndIndexNumber of this.values.entries()) {
      ids.push(shortName(idAndIndexNumber[0], 1), idAndIndexNumber);
    }

    let n = 1;
    while (ids.size > 0) {
      n++;
      for (const [snKey, artifacts] of [...ids.entries()]) {
        // remove it from the list.
        ids.delete(snKey);
        if (artifacts.length === 1) {
          // keep this one, it's unique
          this.identities.set(snKey, artifacts[0][1]);
          this.idShortName.set(artifacts[0][0], snKey);
        } else {
          for (const each of artifacts) {
            ids.push(shortName(each[0], n), each);
          }
        }
      }
    }
  }

  /** @internal */
  override cloneKey(from: this) {
    super.cloneKey(from);
    this.identities = from.identities.greedyClone();
    this.idShortName = new Map(from.idShortName);
  }

  getShortNameOf(id: string) {
    return this.idShortName.get(id);
  }

  nameOrShortNameIs(value: string): TIndexSchema {
    if (value !== undefined && value !== '') {
      const matches = this.identities.get(value);
      if (matches) {
        this.indexSchema.filter(matches);
      }
      else {
        return this.equals(value);
      }
    }
    return this.indexSchema;
  }

  /** deserializes an object graph back into this key */
  override deserialize(content: any) {
    super.deserialize(content);
    this.doneInsertion();
  }
}

/** An key for string values. Does not support 'word' searches */
export class SemverKey<TGraph extends Object, TIndex extends IndexSchema<TGraph, any>> extends Key<TGraph, SemVer, TIndex> {
  compare(a: SemVer, b: SemVer): number {
    return a.compare(b);
  }
  coerce(value: SemVer | string): SemVer {
    if (typeof value === 'string') {
      return new SemVer(value);
    }
    return value;
  }
  protected override  addWord(each: SemVer, n: number) {
    // no parts
  }

  rangeMatch(value: Range | string) {

    // This could be faster if we stored a reverse lookup
    // array that had the id for each key, but .. I don't
    // think the perf will suffer much doing it this way.

    const set = new Set<number>();
    const range = new Range(value);

    for (const node of this.values.entries()) {
      for (const id of node[1]) {

        if (!this.indexSchema.selectedElements || this.indexSchema.selectedElements.has(id)) {
          // it's currently in the keep list.
          if (range.test(node[0])) {
            set.add(id);
          }
        }
      }
    }

    this.indexSchema.filter(set.values());
    return this.indexSchema;
  }

  override serialize() {
    const result = super.serialize();
    result.words = undefined;

    return result;
  }
}

/**
 * Base class for a custom IndexSchema
 *
 * @param TGraph - the object kind to be indexing
 * @param TSelf - the child class that is being constructed.
 */
export abstract class IndexSchema<TGraph, TSelf extends IndexSchema<TGraph, any>> {
  /** the collection of keys in this IndexSchema */
  readonly mapOfKeyObjects = new Map<string, Key<TGraph, any, TSelf>>();

  /**
   * the selected element ids.
   *
   * if this is `undefined`, the whole set is currently selected
   */
  selectedElements?: Set<number>;

  /**
   * filter the selected elements down to an intersetction of the {selectedelements} ∩ {idsToKeep}
   *
   * @param idsToKeep the element ids to intersect with.
   */
  filter(idsToKeep: Iterable<number>) {
    if (this.selectedElements) {
      const selected = new Set<number>();
      for (const each of idsToKeep) {
        if (this.selectedElements.has(each)) {
          selected.add(each);
        }
      }
      this.selectedElements = selected;
    } else {
      this.selectedElements = new Set<number>(idsToKeep);
    }
  }

  /**
   * Serializes this IndexSchema to a persistable object graph.
   */
  serialize() {
    const result = <any>{
    };
    for (const [key, impl] of this.mapOfKeyObjects.entries()) {
      result[key] = impl.serialize();
    }
    return result;
  }

  /**
   * Deserializes a persistable object graph into the IndexSchema.
   *
   * replaces any existing data in the IndexSchema.
   * @param content the persistable object graph.
   */
  deserialize(content: any) {
    for (const [key, impl] of this.mapOfKeyObjects.entries()) {
      impl.deserialize(content[key]);
    }
  }

  /**
   * returns the selected
   */
  get items(): Array<string> {
    return this.selectedElements ? [...this.selectedElements].map(each => this.index.indexOfTargets[each]) : this.index.indexOfTargets;
  }

  /** @internal */
  constructor(public index: Index<TGraph, TSelf>) {
  }
}


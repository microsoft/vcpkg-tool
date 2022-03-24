// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary } from '../interfaces/collections';
import { BaseMap } from './BaseMap';
import { EntityFactory, Node, Yaml, YAMLDictionary } from './yaml-types';


export /** @internal */ abstract class EntityMap<TNode extends Node, TElement extends Yaml<TNode>> extends BaseMap implements Dictionary<TElement>, Iterable<[string, TElement]> {
  protected constructor(protected factory: EntityFactory<TNode, TElement>, node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(node, parent, key);
  }

  get values(): Iterable<TElement> {
    return this.exists() ? this.node.items.map(each => new this.factory(each.value)) : [];
  }

  *[Symbol.iterator](): Iterator<[string, TElement]> {
    if (this.node) {
      for (const each of this.node.items) {
        const k = this.asString(each.key);
        if (k) {
          yield [k, new this.factory(each.value)];
        }
      }
    }
  }

  add(key: string): TElement {
    if (this.has(key)) {
      return this.get(key)!;
    }
    this.assert(true);
    const child = this.factory.create();
    this.set(key, <any>child);
    return new this.factory(this.factory.create(), this, key);
  }

  get(key: string): TElement | undefined {
    return this.getEntity<TNode, TElement>(key, this.factory);
  }

  set(key: string, value: TElement) {
    if (value === undefined || value === null) {
      throw new Error('Cannot set undefined or null to a map');
    }

    if (value.empty) {
      throw new Error('Cannot set an empty entity to a map');
    }

    // if we don't have a node at the moment, we need to create one.
    this.assert(true);

    this.node.set(key, value.node);
  }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isScalar, isSeq } from 'yaml';
import { Entity } from './Entity';
import { ScalarSequence } from './ScalarSequence';
import { EntityFactory, Node, Primitive, Yaml, YAMLSequence } from './yaml-types';


export /** @internal */ abstract class BaseMap extends Entity {
  get keys(): Array<string> {
    return this.exists() ? this.node.items.map(each => this.asString(each.key)!) : [];
  }

  get length(): number {
    return this.exists() ? this.node.items.length : 0;
  }

  getEntity<TNode extends Node, TEntity extends Yaml<TNode> = Yaml<TNode>>(key: string, factory: EntityFactory<TNode, TEntity>): TEntity | undefined {
    if (this.exists()) {
      const v = this.node.get(key, true);
      if (v) {
        return new factory(<any>v, this, key);
      }
    }
    return undefined;
  }

  getSequence(key: string, factory: EntityFactory<YAMLSequence, Entity> | (new (node: Node, parent?: Yaml, key?: string) => ScalarSequence<Primitive>)) {
    if (this.exists()) {
      const v = this.node.get(key, true);
      if (isSeq(v)) {
        return new factory(<any>v);
      }
    }
    return undefined;
  }

  getValue(key: string): Primitive | undefined {
    if (this.exists()) {
      const v = this.node.get(key, true);
      if (isScalar(v)) {
        return this.asPrimitive(v.value);
      }
    }
    return undefined;
  }

  delete(key: string) {
    let result = false;
    if (this.node) {
      result = this.node.delete(key);
    }
    this.dispose();
    return result;
  }

  clear() {
    if (isMap(this.node) || isSeq(this.node)) {
      this.node.items.length = 0;
    }
    this.dispose(true);
  }
}

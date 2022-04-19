// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isScalar, Scalar } from 'yaml';
import { BaseMap } from './BaseMap';
import { EntityFactory, Yaml, YAMLDictionary } from './yaml-types';


export /** @internal */ class CustomScalarMap<TElement extends Yaml<Scalar>> extends BaseMap {
  protected constructor(protected factory: EntityFactory<Scalar, TElement>, node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(node, parent, key);
  }

  add(key: string): TElement {
    this.assert(true);
    this.node.set(key, '');
    return this.get(key)!;
  }


  *[Symbol.iterator](): Iterator<[string, TElement]> {
    if (this.node) {
      for (const { key, value } of this.node.items) {
        if (isScalar(value)) {
          yield [key, new this.factory(value, this, key)];
        }
      }
    }
  }

  get(key: string): TElement | undefined {
    if (this.node) {
      const v = this.node.get(key, true);
      if (isScalar(v)) {
        return new this.factory(v, this, key);
      }
    }
    return undefined;
  }

  set(key: string, value: TElement) {
    if (value === undefined || value === null) {
      throw new Error('Cannot set undefined or null to a map');
    }

    if (value.empty) {
      throw new Error('Cannot set an empty entity to a map');
    }

    this.assert(true);   // if we don't have a node at the moment, we need to create one.

    this.node.set(key, new Scalar(value));
  }
}

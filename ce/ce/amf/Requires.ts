// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Scalar } from 'yaml';
import { VersionReference as IVersionReference } from '../interfaces/metadata/version-reference';
import { CustomScalarMap } from '../yaml/CustomScalarMap';
import { Yaml, YAMLDictionary } from '../yaml/yaml-types';
import { VersionReference } from './version-reference';

export class Requires extends CustomScalarMap<VersionReference> {
  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(VersionReference, node, parent, key);
  }

  override set(key: string, value: VersionReference | IVersionReference | string) {
    if (typeof value === 'string') {
      this.assert(true);   // if we don't have a node at the moment, we need to create one.
      this.node.set(key, new Scalar(value));
      return;
    }
    if (value.raw) {
      this.assert(true);   // if we don't have a node at the moment, we need to create one.
      this.node.set(key, new Scalar(value.raw));
    }
    if (value.resolved) {
      this.assert(true);   // if we don't have a node at the moment, we need to create one.
      this.node.set(key, new Scalar(`${value.range} ${value.resolved}`));
    } else {
      this.assert(true);   // if we don't have a node at the moment, we need to create one.
      this.node.set(key, new Scalar(value.range));
    }
  }
}
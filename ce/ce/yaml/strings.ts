// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary, Strings as IStrings } from '../interfaces/collections';
import { EntityMap } from './EntityMap';
import { ScalarSequence } from './ScalarSequence';
import { Yaml, YAMLDictionary, YAMLScalar, YAMLSequence } from './yaml-types';


export class Strings extends ScalarSequence<string> implements IStrings {
  constructor(node?: YAMLSequence | YAMLScalar, parent?: Yaml, key?: string) {
    super(node, parent, key);
  }
}

export class StringsMap extends EntityMap<YAMLSequence | YAMLScalar, ScalarSequence<string>> implements Dictionary<IStrings> {
  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(Strings, node, parent, key);
  }
}
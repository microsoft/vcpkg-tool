// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Scalar } from 'yaml';
import { Yaml, YAMLSequence } from './yaml-types';


export /** @internal */ class Flags extends Yaml<YAMLSequence> {

  has(flag: string) {
    if (this.node) {
      return this.node.items.some(each => each.value === flag);
    }
    return false;
  }

  set(flag: string, value: boolean) {
    this.assert(true);
    if (value) {
      this.node.add(new Scalar(flag));
    } else {
      this.node.delete(flag);
    }
  }
}

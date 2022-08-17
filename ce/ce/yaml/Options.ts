// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Scalar } from 'yaml';
import { ValidationMessage } from '../interfaces/validation-message';
import { Yaml, YAMLSequence } from './yaml-types';


export /** @internal */ class Options extends Yaml<YAMLSequence> {

  has(option: string) {
    if (this.node) {
      return this.node.items.some(each => each.value === option);
    }
    return false;
  }

  set(option: string, value: boolean) {
    this.assert(true);
    if (value) {
      this.node.add(new Scalar(option));
    } else {
      this.node.delete(option);
    }
  }

  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateIsSequence();
  }
}

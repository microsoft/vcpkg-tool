// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { cli } from '../constants';
import { Switch } from '../switch';

export class Debug extends Switch {
  switch = 'debug';
  get help() {
    return [
      i`enables debug mode, displays internal messsages about how ${cli} works`
    ];
  }
}

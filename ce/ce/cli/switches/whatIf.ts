// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class WhatIf extends Switch {
  switch = 'what-if';
  get help() {
    return [
      i`does not actually perform the action, shows only what would be done`
    ];
  }
}

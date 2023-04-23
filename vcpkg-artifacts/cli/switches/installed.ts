// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class Installed extends Switch {
  switch = 'installed';
  get help() {
    return [
      i`shows the _installed_ artifacts`
    ];
  }
}

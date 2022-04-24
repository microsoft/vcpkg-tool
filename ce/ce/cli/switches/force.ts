// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class Force extends Switch {
  switch = 'force';
  get help() {
    return [
      i`proceeds with the (potentially dangerous) action without confirmation`
    ];
  }
}

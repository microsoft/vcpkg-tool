// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class Clear extends Switch {
  switch = 'clear';
  get help() {
    return [
      i`removes all files in the local cache`
    ];
  }
}

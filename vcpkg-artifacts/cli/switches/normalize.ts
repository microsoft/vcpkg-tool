// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class Normalize extends Switch {
  switch = 'normalize';
  override multipleAllowed = false;
  get help() {
    return [
      i`Apply any deprecation fixups.`
    ];
  }
}

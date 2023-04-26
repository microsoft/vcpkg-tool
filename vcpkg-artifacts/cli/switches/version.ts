// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class Version extends Switch {
  switch = 'version';
  get help() {
    return [
      i`a version or version range to match`
    ];
  }
}
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class All extends Switch {
  switch = 'all';
  get help() {
    return [
      i`Update all known artifact registries`
    ];
  }
}

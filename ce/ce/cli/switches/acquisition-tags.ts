// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { Switch } from '../switch';

export class AcquisitionTags extends Switch {
  switch = 'tags';
  override multipleAllowed = false;

  get help() {
    return [
      i`The set of tags to filter by; artifacts not matching the supplied tags will not be acquired.`
    ];
  }
}

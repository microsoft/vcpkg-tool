// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Switch } from '../switch';

export class Json extends Switch {
  switch = 'json';
  override multipleAllowed = false;
  get help() {
    return [
      i`Dump environment variables and other properties to a json file with the path provided by the user.`
    ];
  }

  get resolvedValue(): Uri | undefined {
    const v = this.value;
    if (v) {
      return session.fileSystem.file(resolve(v));
    }

    return undefined;
  }

}

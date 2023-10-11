// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Switch } from '../switch';

export class Json extends Switch {
  switch = 'json';

  get resolvedValue(): Uri | undefined {
    const v = this.value;
    if (v) {
      return session.fileSystem.file(resolve(v));
    }

    return undefined;
  }

}

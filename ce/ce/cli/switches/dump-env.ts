// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { resolvePath } from '../command-line';
import { Switch } from '../switch';

export class DumpEnv extends Switch {
  
  switch = 'dump-env';
  override multipleAllowed = false;
  get help() {
    return [
      i`Dump environment variables to a json file with the path provided by the user.`
    ];
  }

  override get value(): Uri | undefined {
    const v = resolvePath(super.value);
    return v ? session.fileSystem.file(v) : undefined;
  }

}

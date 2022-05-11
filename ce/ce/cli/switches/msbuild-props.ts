// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { resolvePath } from '../command-line';
import { Switch } from '../switch';

export class MSBuildProps extends Switch {
  switch = 'msbuild-props';
  override multipleAllowed = false;
  get help() {
    return [
      i`Full path to the file in which MSBuild properties will be written.`
    ];
  }

  override get value(): Uri | undefined {
    const v = resolvePath(super.value);
    return v ? session.fileSystem.file(v) : undefined;
  }
}

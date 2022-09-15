// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { i } from '../../i18n';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { Switch } from '../switch';

export class MSBuildProps extends Switch {
  public readonly switch: string;
  override multipleAllowed = false;
  constructor(command: Command, swName = 'msbuild-props') {
    super(command);
    this.switch = swName;
  }

  get help() {
    return [
      i`Full path to the file in which MSBuild properties will be written.`
    ];
  }

  override get value(): Uri | undefined {
    const v = super.value;
    if (v) {
      return session.fileSystem.file(resolve(v));
    }

    return undefined;
  }
}

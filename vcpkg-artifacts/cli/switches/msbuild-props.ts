// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { resolve } from 'path';
import { session } from '../../main';
import { Uri } from '../../util/uri';
import { Command } from '../command';
import { Switch } from '../switch';

export class MSBuildProps extends Switch {
  public readonly switch: string;
  constructor(command: Command, swName = 'msbuild-props') {
    super(command);
    this.switch = swName;
  }

  get resolvedValue(): Uri | undefined {
    const v = this.value;
    if (v) {
      return session.fileSystem.file(resolve(v));
    }

    return undefined;
  }
}

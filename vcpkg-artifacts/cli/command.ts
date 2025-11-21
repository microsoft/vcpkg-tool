// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Argument } from './argument';
import { CommandLine } from './command-line';
import { Switch } from './switch';
import { Debug } from './switches/debug';
import { Force } from './switches/force';

/** @internal */

export abstract class Command {
  readonly abstract command: string;

  readonly switches = new Array<Switch>();
  readonly arguments = new Array<Argument>();

  readonly force = new Force(this);
  readonly debug = new Debug(this);

  constructor(public commandLine: CommandLine) {}

  get inputs() {
    return this.commandLine.inputs.slice(1);
  }

  async run() {
    // do something
    return true;
  }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Command } from './command';
import { Help } from './command-line';

export abstract class Argument implements Help {
  readonly abstract argument: string;
  readonly title = '';
  readonly abstract help: Array<string>;

  constructor(protected command: Command) {
    command.arguments.push(this);
  }
}

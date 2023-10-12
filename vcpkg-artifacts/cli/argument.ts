// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Command } from './command';

export abstract class Argument {
  readonly abstract argument: string;
  readonly title = '';

  constructor(protected command: Command) {
    command.arguments.push(this);
  }
}

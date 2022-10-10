// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { projectFile } from '../format';
import { log } from '../styling';

export class DeactivateCommand extends Command {
  readonly command = 'deactivate';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];

  get summary() {
    return i`Deactivates the current session`;
  }

  get description() {
    return [
      i`This allows the consumer to remove environment settings for the currently active session.`,
    ];
  }

  override async run() {
    const project = await session.findProjectProfile();
    if (project) {
      log(i`Deactivating project ${projectFile(project)}`);
    }

    await session.deactivate();
    return true;
  }
}

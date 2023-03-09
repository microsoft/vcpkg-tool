// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { projectFile } from '../format';
import { log } from '../styling';
import { Project } from '../switches/project';

export class DeactivateCommand extends Command {
  readonly command = 'deactivate';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  project = new Project(this);

  get summary() {
    return i`Deactivates the current session`;
  }

  get description() {
    return [
      i`This allows the consumer to remove environment settings for the currently active session.`,
    ];
  }

  override async run() {
    const project = await this.project.resolvedValue;
    if (project) {
      log(i`Deactivating project ${projectFile(project)}`);
    }

    await session.deactivate();
    return true;
  }
}

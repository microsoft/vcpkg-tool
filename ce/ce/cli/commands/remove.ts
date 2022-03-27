// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { projectFile } from '../format';
import { activateProject } from '../project';
import { debug, error, log } from '../styling';
import { Project } from '../switches/project';
import { WhatIf } from '../switches/whatIf';

export class RemoveCommand extends Command {
  readonly command = 'remove';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  whatIf = new WhatIf(this);
  project: Project = new Project(this);

  get summary() {
    return i`Removes an artifact from a project`;
  }

  get description() {
    return [
      i`This allows the consumer to remove an artifact from the project. Forces reactivation in this window.`,
    ];
  }

  override async run() {
    const projectManifest = await this.project.manifest;

    if (!projectManifest) {
      error(i`Unable to find project in folder (or parent folders) for ${session.currentDirectory.fsPath}`);
      return false;
    }

    if (this.inputs.length === 0) {
      error(i`No artifacts specified`);
      return false;
    }


    const req = projectManifest.metadata.requires.keys;
    for (const input of this.inputs) {
      if (req.indexOf(input) !== -1) {
        projectManifest.metadata.requires.delete(input);
        log(i`Removing ${input} from project manifest`);
      } else {
        error(i`unable to find artifact ${input} in the project manifest`);
        return false;
      }
    }

    // write the file out.
    await projectManifest.metadata.save();

    debug(`Deactivating project ${projectFile(projectManifest.metadata.context.file)}`);
    await session.deactivate();

    return await activateProject(projectManifest, this.commandLine);
  }
}
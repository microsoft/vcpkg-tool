// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { error, log } from '../styling';

export class RemoveCommand extends Command {
  readonly command = 'remove';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];

  get summary() {
    return i`Removes an artifact from a project`;
  }

  get description() {
    return [
      i`This allows the consumer to remove an artifact from the project. Forces reactivation in this window.`,
    ];
  }

  override async run() {
    const projectManifest = await session.loadRequiredProjectProfile();
    if (!projectManifest) {
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
    return true;
  }
}

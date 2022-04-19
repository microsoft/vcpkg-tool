// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { Version } from '../switches/version';
import { WhatIf } from '../switches/whatIf';

export class DeleteCommand extends Command {
  readonly command = 'delete';
  readonly aliases = ['uninstall'];
  seeAlso = [];
  argumentsHelp = [];
  version = new Version(this);
  whatIf = new WhatIf(this);

  get summary() {
    return i`Deletes an artifact from the artifact folder`;
  }

  get description() {
    return [
      i`This allows the consumer to remove an artifact from disk.`,
    ];
  }

  override async run() {
    const artifacts = await session.getInstalledArtifacts();
    for (const input of this.inputs) {
      for (const { artifact, id, folder } of artifacts) {
        if (input === id) {
          if (await folder.exists()) {
            session.channels.message(i`Deleting artifact ${id} from ${folder.fsPath}`);
            await artifact.uninstall();
          }
        }
      }
    }
    return true;
  }
}
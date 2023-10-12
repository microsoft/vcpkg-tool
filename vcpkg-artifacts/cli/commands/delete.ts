// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { Version } from '../switches/version';

export class DeleteCommand extends Command {
  readonly command = 'delete';
  version = new Version(this);
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

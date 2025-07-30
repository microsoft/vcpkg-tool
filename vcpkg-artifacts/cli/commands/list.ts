// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { session } from '../../main';
import { Command } from '../command';
import { Table } from '../console-table';
import { artifactIdentity } from '../format';
import { log } from '../styling';
import { Installed } from '../switches/installed';

export class ListCommand extends Command {
  readonly command = 'list';
  installed = new Installed(this);

  override async run() {
    if (this.installed.active) {
      const artifacts = await session.getInstalledArtifacts();
      const table = new Table('Artifact', 'Version', 'Summary');

      for (const { artifact, id } of artifacts) {
        const name = artifactIdentity('<registry-name-goes-here>', id, artifact.shortName); //todo: fixme
        table.push(name, artifact.version, artifact.metadata.summary || '');
      }
      log(table.toString());
      log();
    }
    else {
      log('use --installed for now');
    }

    return true;
  }
}

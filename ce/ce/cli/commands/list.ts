// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { artifactIdentity } from '../format';
import { Table } from '../markdown-table';
import { log } from '../styling';
import { Installed } from '../switches/installed';

export class ListCommand extends Command {
  readonly command = 'list';
  readonly aliases = ['show'];
  seeAlso = [];
  argumentsHelp = [];
  installed = new Installed(this);

  get summary() {
    return i`Lists the artifacts`;
  }

  get description() {
    return [
      i`This allows the consumer to list artifacts.`,
    ];
  }

  override async run() {
    if (this.installed.active) {
      const artifacts = await session.getInstalledArtifacts();
      const table = new Table('Artifact', 'Version', 'Summary');

      for (const { artifact, id, folder } of artifacts) {

        const name = artifactIdentity('<registry-name-goes-here>', id); //todo: fixme
        table.push(name, artifact.version, artifact.metadata.info.summary || '');
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
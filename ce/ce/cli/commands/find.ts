// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { cyan } from 'chalk';
import { i } from '../../i18n';
import { session } from '../../main';
import { Registries } from '../../registries/registries';
import { Command } from '../command';
import { artifactIdentity } from '../format';
import { Table } from '../markdown-table';
import { debug, error, log } from '../styling';
import { Project } from '../switches/project';
import { Registry } from '../switches/registry';
import { Version } from '../switches/version';

export class FindCommand extends Command {
  readonly command = 'find';
  readonly aliases = ['search'];
  seeAlso = [];
  argumentsHelp = [];

  version = new Version(this);
  registrySwitch = new Registry(this);
  project = new Project(this);

  get summary() {
    return i`Find artifacts in the registry`;
  }

  get description() {
    return [
      i`This allows the user to find artifacts based on some criteria.`,
    ];
  }

  override async run() {
    // load registries (from the current project too if available)
    let registries: Registries = await this.registrySwitch.loadRegistries(session);
    registries = (await this.project.manifest)?.registries ?? registries;

    debug(`using registries: ${[...registries].map(([registry, registryNames]) => registryNames[0]).join(', ')}`);
    const table = new Table('Artifact', 'Version', 'Summary');

    for (const each of this.inputs) {
      const hasColon = each.indexOf(':') > -1;
      // eslint-disable-next-line prefer-const
      for (let [registry, id, artifacts] of await registries.search({
        // use keyword search if no registry is specified
        keyword: hasColon ? undefined : each,
        // otherwise use the criteria as an id
        idOrShortName: hasColon ? each : undefined,
        version: this.version.value
      })) {
        if (!this.version.isRangeOfVersions) {
          // if the user didn't specify a range, just show the latest version that was returned
          artifacts = [artifacts[0]];
        }
        for (const result of artifacts) {
          if (!result.metadata.dependencyOnly) {
            const name = artifactIdentity(result.registryId, id, result.shortName);
            table.push(name, result.metadata.version, result.metadata.summary || '');
          }
        }
      }
    }
    if (!table.anyRows) {
      error(i`No artifacts found matching criteria: ${cyan.bold(this.inputs.join(', '))}`);
      return false;
    }

    log(table.toString());
    log();
    return true;
  }
}

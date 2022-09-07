// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { cyan } from 'chalk';
import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { Table } from '../markdown-table';
import { error, log } from '../styling';
import { Project } from '../switches/project';
import { Version } from '../switches/version';

export class FindCommand extends Command {
  readonly command = 'find';
  readonly aliases = ['search'];
  seeAlso = [];
  argumentsHelp = [];
  version = new Version(this);
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
    const resolver = await session.loadDefaultRegistryResolver(await this.project.manifest);
    const table = new Table('Artifact', 'Version', 'Summary');

    for (const each of this.inputs) {
      const hasColon = each.indexOf(':') > -1;
      // eslint-disable-next-line prefer-const
      for (let [display, artifactVersions] of await resolver.search({
        // use keyword search if no registry is specified
        keyword: hasColon ? undefined : each,
        // otherwise use the criteria as an id
        idOrShortName: hasColon ? each : undefined,
        version: this.version.value
      })) {
        if (!this.version.isRangeOfVersions) {
          // if the user didn't specify a range, just show the latest version that was returned
          artifactVersions = [artifactVersions[0]];
        }
        for (const result of artifactVersions) {
          if (!result.metadata.dependencyOnly) {
            table.push(display, result.metadata.version, result.metadata.summary || '');
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

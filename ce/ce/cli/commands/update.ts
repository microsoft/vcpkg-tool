// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver } from '../../artifacts/artifact';
import { i } from '../../i18n';
import { session } from '../../main';
import { Registry } from '../../registries/registries';
import { RemoteFileUnavailable } from '../../util/exceptions';
import { Command } from '../command';
import { CommandLine } from '../command-line';
import { count } from '../format';
import { error, log, writeException } from '../styling';
import { Project } from '../switches/project';

export class UpdateCommand extends Command {
  readonly command = 'update';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  project: Project = new Project(this);

  get summary() {
    return i`update the registry from the remote`;
  }

  get description() {
    return [
      i`This downloads the latest contents of the registry from the remote service.`,
    ];
  }

  override async run() {
    const resolver = session.globalRegistryResolver.with(
      await buildRegistryResolver(session, (await this.project.manifest)?.metadata.registries));
    for (const registryName of this.inputs) {
      const registry = resolver.getRegistryByName(registryName);
      if (registry) {
        try {
          log(i`Downloading registry data`);
          await registry.update();
          await registry.load();
          log(i`Updated ${registryName}. registry contains ${count(registry.count)} metadata files`);
        } catch (e) {
          if (e instanceof RemoteFileUnavailable) {
            log(i`Unable to download registry snapshot`);
            return false;
          }
          writeException(e);
          return false;
        }
      } else {
        error(i`Unable to find registry ${registryName}`);
      }
    }

    return true;
  }

  static async update(registry: Registry) {
    log(i`Artifact registry data is not loaded`);
    log(i`Attempting to update artifact registry`);
    const update = new UpdateCommand(new CommandLine([]));

    let success = true;
    try {
      success = await update.run();
    } catch (e) {
      writeException(e);
      success = false;
    }
    if (!success) {
      error(i`Unable to load registry index`);
      return false;
    }
    try {
      await registry.load();
    } catch (e) {
      writeException(e);
      // it just doesn't want to load.
      error(i`Unable to load registry index`);
      return false;
    }
    return true;
  }
}

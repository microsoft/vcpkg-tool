// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { buildRegistryResolver } from '../../artifacts/artifact';
import { schemeOf } from '../../fs/unified-filesystem';
import { i } from '../../i18n';
import { session } from '../../main';
import { RemoteRegistry } from '../../registries/RemoteRegistry';
import { Registry } from '../../registries/registries';
import { RemoteFileUnavailable } from '../../util/exceptions';
import { Command } from '../command';
import { count } from '../format';
import { error, log, writeException } from '../styling';
import { All } from '../switches/all';
import { Project } from '../switches/project';

async function updateRegistry(registry: Registry, displayName: string) : Promise<boolean> {
  try {
    await registry.update(displayName);
    await registry.load();
    log(i`Updated ${displayName}. It contains ${count(registry.count)} metadata files.`);
  } catch (e) {
    if (e instanceof RemoteFileUnavailable) {
      log(i`Unable to download ${displayName}.`);
    } else {
      log(i`${displayName} could not be updated; it could be malformed.`);
      writeException(e);
    }

    return false;
  }

  return true;
}

export class UpdateCommand extends Command {
  readonly command = 'update';

  project: Project = new Project(this);
  all = new All(this);

  override async run() {
    const resolver = session.globalRegistryResolver.with(
      await buildRegistryResolver(session, (await this.project.manifest)?.metadata.registries));

    if (this.all.active) {
      for (const registryUri of session.registryDatabase.getAllUris()) {
        if (schemeOf(registryUri) != 'https') { continue; }
        const parsed = session.fileSystem.parseUri(registryUri);
        const displayName = resolver.getRegistryDisplayName(parsed);
        const loaded = resolver.getRegistryByUri(parsed);
        if (loaded) {
          if (!await updateRegistry(loaded, displayName)) {
            return false;
          }
        }
      }
    }

    for (const registryInput of this.inputs) {
      const registryByName = resolver.getRegistryByName(registryInput);
      if (registryByName) {
        // if it matched a name, it's a name
        if (!await updateRegistry(registryByName, registryInput)) {
          return false;
        }

        continue;
      }

      const scheme = schemeOf(registryInput);
      switch (scheme) {
        case 'https':
        {
          const registryInputAsUri = session.fileSystem.parseUri(registryInput);
          const registryByUri = resolver.getRegistryByUri(registryInputAsUri)
            ?? new RemoteRegistry(session, registryInputAsUri);
          if (!await updateRegistry(registryByUri, resolver.getRegistryDisplayName(registryInputAsUri))) {
            return false;
          }

          continue;
        }
        case 'file':
          error(i`The x-update-registry command downloads new registry information and thus cannot be used with local registries. Did you mean x-regenerate ${registryInput}?`);
          return false;
      }

      error(i`Unable to find registry ${registryInput}.`);
      return false;
    }

    return true;
  }
}

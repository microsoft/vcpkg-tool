// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { Artifact, parseArtifactName } from '../artifacts/artifact';
import { Registry, SearchCriteria } from '../artifacts/registry';
import { i } from '../i18n';
import { Session } from '../session';
import { linq } from '../util/linq';
import { Uri } from '../util/uri';

export class Registries implements Iterable<[Registry, Array<string>]> {
  #registries: Map<string, Registry> = new Map();
  #uriToName: Map<string, string> = new Map();

  constructor(protected session: Session) {

  }

  [Symbol.iterator](): Iterator<[Registry, Array<string>]> {
    return linq.entries(this.#registries).groupBy(([name, registry]) => registry, ([name, registry]) => name).entries();
  }

  getRegistryDisplayName(registry: Uri): string {
    const stringized = registry.toString();
    const prettyName = this.#uriToName.get(stringized);
    if (prettyName) { return prettyName; }
    return '[' + stringized + ']';
  }

  getRegistry(id: string | Uri): Registry | undefined {
    return this.#registries.get(id.toString());
  }

  has(registryName?: string) {
    // only check for registries names not locations.
    if (registryName && registryName.indexOf('://') === -1) {
      for (const [name] of this.#registries) {
        if (name === registryName && name.indexOf('://') === -1) {
          return true;
        }
      }
    }
    return false;
  }

  add(registry: Registry, name?: string): Registry {
    const location = registry.location;

    // check if this is already recorded (by uri)
    let r = this.#registries.get(location.toString());
    if (r && r !== registry) {
      throw new Error(`Registry with location ${location.toString()} already loaded in this context`);
    }

    // check if this is already recorded (by common name)
    if (name) {
      r = this.#registries.get(name);
      if (r && r !== registry) {
        throw new Error(`Registry with a different name ${name} already loaded in this context`);
      }
      this.#registries.set(name, registry);
      this.#uriToName.set(location.toString(), name);
    }

    // record it by uri
    this.#registries.set(location.toString(), registry);

    return registry;
  }

  async search(criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>> {
    const [source, name] = parseArtifactName(criteria?.idOrShortName || '');
    if (source === undefined) {
      // search them all
      return (await Promise.all([...this].map(async ([registry,]) => await registry.search(this, criteria)))).flat();
    } else {
      const registry = this.getRegistry(source);
      if (registry) {
        return registry.search(this, { ...criteria, idOrShortName: name });
      }

      throw new Error('Unknown registry ' + source);
    }
  }

  /**
  * returns an artifact for the strongly-named artifact id/version.
  *
  * @param idOrShortName the identity of the artifact. If the string has no '<source>:' at the front, default source is assumed.
  * @param version the version of the artifact
  */
  async getArtifact(idOrShortName: string, version: string | undefined): Promise<[Registry, string, Artifact] | undefined> {
    const artifacts = await this.search({ idOrShortName, version });

    switch (artifacts.length) {
      case 0:
        // did not match a name or short name.
        return undefined; // nothing matched.

      case 1: {
        // found the artifact. awesome.
        const [registry, artifactId, all] = artifacts[0];
        return [registry, artifactId, all[0]];
      }

      default: {
        // multiple matches.
        // we can't return a single artifact, we're going to have to throw.
        fail(i`Artifact identity '${idOrShortName}' matched more than one result (${[...artifacts.map(each => each[1])].join(',')}).`);
      }
    }
  }
}

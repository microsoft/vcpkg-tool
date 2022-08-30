// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { Artifact, parseArtifactDependency } from '../artifacts/artifact';
import { artifactIdentity } from '../cli/format';
import { i } from '../i18n';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { LocalRegistry } from './LocalRegistry';
import { RemoteRegistry } from './RemoteRegistry';

export interface SearchCriteria {
  idOrShortName?: string;
  version?: string
  keyword?: string;
}

// In general, a Registry or RegistryResolverContext
export interface ArtifactSearchable {
  // Returns [artifactId, artifactsOfMatchingVersionsOfThatId][]
  search(criteria?: SearchCriteria): Promise<Array<[string, Array<Artifact>]>>;
}

export interface Registry extends ArtifactSearchable {
  readonly count: number;
  readonly location: Uri;

  load(force?: boolean): Promise<void>;
  save(): Promise<void>;
  update(): Promise<void>;
  regenerate(normalize?: boolean): Promise<void>;
}

/**
  * returns an artifact for the strongly-named artifact id/version.
  */
export async function getArtifact(registry: ArtifactSearchable, idOrShortName: string, version: string | undefined): Promise<[string, Artifact] | undefined> {
  const artifactRecords = await registry.search({ idOrShortName, version });
  if (artifactRecords.length === 0) {
    return undefined; // nothing matched.
  }

  if (artifactRecords.length === 1) {
    // found 1 matching artifact identity
    const artifactRecord = artifactRecords[0];
    const artifactDisplay = artifactRecord[0];
    const artifactVersions = artifactRecord[1];
    if (artifactVersions.length === 0) {
      throw new Error('Internal search error: id matched but no versions present');
    }

    return [artifactDisplay, artifactVersions[0]];
  }

  // multiple matches.
  // we can't return a single artifact, we're going to have to throw.
  fail(i`'${idOrShortName}' matched more than one result (${[...artifactRecords.map(each => each[0])].join(',')}).`);
}

export class RegistryDatabase {
  #uriToRegistry: Map<string, Registry> = new Map();

  getRegistryByUri(registryUri: string) {
    return this.#uriToRegistry.get(registryUri);
  }

  entries() { return this.#uriToRegistry.values(); }

  has(registryUri: string) { return this.#uriToRegistry.has(registryUri); }

  async loadRegistry(session: Session, registryLocation: Uri | string): Promise<Registry> {
    // normalize the location first.
    let locationUri: Uri;
    if (typeof registryLocation === 'string') {
      locationUri = await session.parseLocation(registryLocation);
    } else {
      locationUri = <Uri>registryLocation;
    }

    const locationUriStr = locationUri.toString();
    const existingRegistry = this.#uriToRegistry.get(locationUriStr);
    if (existingRegistry) {
      return existingRegistry;
    }

    // not already loaded
    let loaded: Registry;
    switch (locationUri.scheme) {
      case 'https':
        loaded = new RemoteRegistry(session, locationUri);
        break;

      case 'file':
        loaded = new LocalRegistry(session, locationUri);
        break;

      default:
        throw new Error(i`Unsupported registry scheme '${locationUri.scheme}'`);
    }

    this.#uriToRegistry.set(locationUriStr, loaded);
    await loaded.load();
    return loaded;
  }
}

// When a registry resolver is used to map a URI back to some form of for-display-purposes-only name.
export interface RegistryDisplayContext {
  getRegistryDisplayName(registry: Uri): string;
}

export class RegistryResolver implements RegistryDisplayContext {
  readonly #database: RegistryDatabase;
  readonly #knownUris: Set<string>;
  readonly #uriToName: Map<string, string>;
  readonly #nameToUri: Map<string, string>;

  private addMapping(name: string, uri: string) {
    this.#uriToName.set(uri, name);
    this.#nameToUri.set(name, uri);
  }

  private getLocation(registry: Registry): string {
    const location = registry.location;
    const stringized = location.toString();
    if (!this.#database.has(stringized)) {
      throw new Error('Attempted to add unloaded registry to a RegistryContext');
    }

    return stringized;
  }

  constructor(parent: RegistryDatabase | RegistryResolver) {
    if (parent instanceof RegistryResolver) {
      this.#database = parent.#database;
      this.#knownUris = new Set(parent.#knownUris);
      this.#uriToName = new Map(parent.#uriToName);
      this.#nameToUri = new Map(parent.#nameToUri);
    } else {
      this.#database = parent;
      this.#knownUris = new Set();
      this.#uriToName = new Map();
      this.#nameToUri = new Map();
    }
  }

  getRegistryName(registry: Uri): string | undefined {
    const stringized = registry.toString();
    return this.#uriToName.get(stringized);
  }

  getRegistryDisplayName(registry: Uri): string {
    const stringized = registry.toString();
    const prettyName = this.#uriToName.get(stringized);
    if (prettyName) {
      return prettyName;
    }

    return `[${stringized}]`;
  }

  getRegistryByUri(registryUri: Uri): Registry | undefined {
    return this.#database.getRegistryByUri(registryUri.toString());
  }

  getRegistryByName(name: string) : Registry | undefined {
    const asUri = this.#nameToUri.get(name);
    if (asUri) {
      return this.#database.getRegistryByUri(asUri);
    }

    return undefined;
  }

  getRegistryByNameOrUri(nameOrUri: string) : Registry | undefined {
    const asUri = this.#uriToName.get(nameOrUri);
    if (asUri) {
      return this.#database.getRegistryByUri(asUri);
    }

    return this.#database.getRegistryByUri(nameOrUri);
  }

  // Adds `registry` to this context with name `name`. If `name` is already set to a different URI, throws.
  add(registry: Registry, name: string) {
    const location = this.getLocation(registry);
    this.#knownUris.add(location);
    const oldLocation = this.#nameToUri.get(name);
    if (oldLocation && oldLocation !== location) {
      throw new Error(i`Tried to add ${location} as ${name}, but ${name} is already ${oldLocation}.`);
    }

    this.addMapping(name, location);
  }

  async search(criteria?: SearchCriteria): Promise<Array<[string, Array<Artifact>]>> {
    const idOrShortName = criteria?.idOrShortName || '';
    const [source, name] = parseArtifactDependency(idOrShortName);
    if (source === undefined) {
      // search them all
      const results : Array<[string, Array<Artifact>]> = [];
      for (const location of this.#knownUris) {
        const registry = this.#database.getRegistryByUri(location);
        if (registry === undefined) {
          throw new Error('RegistryContext tried to search an unloaded registry.');
        }

        const displayName = this.getRegistryDisplayName(registry.location);
        for (const [artifactId, artifacts] of await registry.search(criteria)) {
          results.push([artifactIdentity(displayName, artifactId), artifacts]);
        }
      }

      return results;
    } else {
      const registry = this.getRegistryByName(source);
      if (registry) {
        return (await registry.search({ ...criteria, idOrShortName: name }))
          .map((artifactRecord) => [artifactIdentity(source, artifactRecord[0]), artifactRecord[1]]);
      }

      throw new Error(i`Unknown registry ${source} (in ${idOrShortName}). The following are known: ${Array.from(this.#nameToUri.keys()).join(', ')}`);
    }
  }

  // Combines resolvers together. Any registries that match exactly will take their names from `this`. Any registries
  // whose names match but which resolve to different URIs will have the name from `this`, and the other registry
  // will become known but nameless.
  combineWith(otherResolver: RegistryResolver) : RegistryResolver {
    if (this.#database !== otherResolver.#database) {
      throw new Error('Tried to combine registry resolvers with different databases.');
    }

    const result = new RegistryResolver(this);
    for (const uri of otherResolver.#knownUris) {
      result.#knownUris.add(uri);
    }

    for (const [name, location] of otherResolver.#nameToUri) {
      if (!result.#nameToUri.has(name) && !result.#uriToName.has(location)) {
        result.addMapping(name, location);
      }
    }

    return result;
  }
}

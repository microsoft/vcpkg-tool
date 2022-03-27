// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Artifact } from '../artifacts/artifact';
import { Registry, SearchCriteria } from '../artifacts/registry';
import { FileSystem } from '../fs/filesystem';
import { i } from '../i18n';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { Registries } from './registries';

export class AggregateRegistry extends Registries implements Registry {
  readonly count = 0;

  get loaded(): boolean {
    return true;
  }

  constructor(session: Session) {
    super(session);
  }
  override search(criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>>
  override search(parent: Registries, criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>>
  override async search(parentOrCriteria?: Registries | SearchCriteria, criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>> {
    const parent = parentOrCriteria instanceof Registries ? parentOrCriteria : this;
    criteria = criteria || <SearchCriteria>parentOrCriteria;

    const [source, name] = this.session.parseName(criteria?.idOrShortName || '');
    if (source !== 'default') {
      // search the explicitly asked for registry.
      return this.getRegistryWithNameOrLocation(source).search(parent, { ...criteria, idOrShortName: name });
    }

    // search them all
    return (await Promise.all([...this].map(async ([registry,]) => await registry.search(parent, criteria)))).flat();
  }

  override getRegistryName(registry: Registry): string {
    for (const [name, reg] of this.registries) {
      if (reg === registry) {
        return name;
      }
    }
    // worst-case scenario if we don't have a name in the parent context.
    return registry.location.scheme === 'file' ? `[${registry.location.fsPath}]` : `[${registry.location.toString()}]`;
  }

  async load(force?: boolean): Promise<void> {
    await Promise.all([...this].map(async ([registry,]) => registry.load(force)));
  }

  save(): Promise<void> {
    // nothing to save.
    return Promise.resolve();
  }
  update(): Promise<void> {
    // nothing to update.
    return Promise.resolve();
  }
  regenerate(): Promise<void> {
    // nothing to regenerate
    return Promise.resolve();
  }

  openArtifact(manifestPath: string): Promise<Artifact> {
    throw new Error('Method not implemented.');
  }

  openArtifacts(manifestPaths: Array<string>): Promise<Map<string, Array<Artifact>>> {
    throw new Error('Method not implemented.');
  }

  readonly installationFolder = Uri.parse(<FileSystem><any>undefined, 'artifacts:installFolder');
  readonly location = Uri.invalid;

  override getRegistry(id: string): Registry | undefined {
    if (id === 'default') {
      return this;
    }
    return this.registries.get(id.toString());
  }

  override getRegistryWithNameOrLocation(registryNameOrUri: string) {
    if (registryNameOrUri === 'default') {
      return this;
    }
    const result = this.getRegistry(registryNameOrUri);
    if (!result) {
      throw new Error(i`Unknown registry '${registryNameOrUri}'`);
    }
    return result;
  }
}
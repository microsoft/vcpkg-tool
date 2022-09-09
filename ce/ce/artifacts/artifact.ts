/* eslint-disable prefer-const */
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { resolve } from 'path';
import { MetadataFile } from '../amf/metadata-file';
import { RegistriesDeclaration } from '../amf/registries';
import { artifactIdentity, prettyRegistryName } from '../cli/format';
import { FileType } from '../fs/filesystem';
import { i } from '../i18n';
import { activateEspIdf, installEspIdf } from '../installers/espidf';
import { InstallEvents } from '../interfaces/events';
import { getArtifact, Registry, RegistryResolver } from '../registries/registries';
import { Session } from '../session';
import { linq } from '../util/linq';
import { Uri } from '../util/uri';
import { SetOfDemands } from './SetOfDemands';

export type Selections = Map<string, string>; // idOrShortName, version

export function parseArtifactDependency(id: string): [string | undefined, string] {
  const parts = id.split(':');
  if (parts.length === 2) {
    return [parts[0], parts[1]];
  }

  if (parts.length === 1) {
    return [undefined, parts[0]];
  }

  throw new Error(i`Invalid artifact id '${id}'`);
}

export async function buildRegistryResolver(session: Session, registries: RegistriesDeclaration) {
  // load the registries from the project file
  const result = new RegistryResolver(session.registryDatabase);
  for (const [name, registry] of registries) {
    const loc = registry.location.get(0);
    if (loc) {
      const locUri = session.parseLocation(loc);
      await session.registryDatabase.loadRegistry(session, locUri);
      result.add(locUri, name);
    }
  }

  return result;
}

function addDisplayPrefix(prefix: string, targets: Array<string>): Array<string> {
  const result = new Array<string>();
  for (const element of targets) {
    result.push(i`${prefix} - ${element}`);
  }

  return result;
}

export abstract class ArtifactBase {
  readonly applicableDemands: SetOfDemands;

  constructor(protected session: Session, public readonly metadata: MetadataFile) {
    this.applicableDemands = new SetOfDemands(this.metadata, this.session);
  }

  buildRegistryResolver() : Promise<RegistryResolver> {
    return buildRegistryResolver(this.session, this.metadata.registries);
  }
}

export enum InstallStatus {
  Installed,
  AlreadyInstalled,
  Failed
}

export class Artifact extends ArtifactBase {
  allPaths: Array<string> = [];

  constructor(session: Session, metadata: MetadataFile, public shortName: string, public targetLocation: Uri) {
    super(session, metadata);
  }

  get id() {
    return this.metadata.id;
  }

  get version() {
    return this.metadata.version;
  }

  get registryUri() {
    return this.metadata.registryUri!;
  }

  get isInstalled() {
    return this.targetLocation.exists('artifact.json');
  }

  get uniqueId() {
    return `${this.registryUri.toString()}::${this.id}::${this.version}`;
  }

  async install(thisDisplayName: string, events: Partial<InstallEvents>, options: { force?: boolean, allLanguages?: boolean, language?: string }): Promise<InstallStatus> {
    const applicableDemands = this.applicableDemands;
    const errors = addDisplayPrefix(thisDisplayName, applicableDemands.errors);
    this.session.channels.error(errors);
    if (errors.length) {
      return InstallStatus.Failed;
    }

    this.session.channels.warning(addDisplayPrefix(thisDisplayName, applicableDemands.warnings));
    this.session.channels.message(addDisplayPrefix(thisDisplayName, applicableDemands.messages));

    if (await this.isInstalled && !options.force) {
      if (!await this.loadActivationSettings(events)) {
        throw new Error(i`Failed during artifact activation`);
      }

      return InstallStatus.AlreadyInstalled;
    }

    try {
      if (options.force) {
        try {
          await this.uninstall();
        } catch {
          // if a file is locked, it may not get removed. We'll deal with this later.
        }
      }

      // ok, let's install this.
      for (const installInfo of applicableDemands.installer) {
        if (installInfo.lang && !options.allLanguages && options.language && options.language.toLowerCase() !== installInfo.lang.toLowerCase()) {
          continue;
        }

        const installer = this.session.artifactInstaller(installInfo);
        if (!installer) {
          fail(i`Unknown installer type ${installInfo!.installerKind}`);
        }
        await installer(this.session, this.id, this.version, this.targetLocation, installInfo, events, options);
      }

      // after we unpack it, write out the installed manifest
      await this.writeManifest();
      if (!await this.loadActivationSettings(events)) {
        throw new Error(i`Failed during artifact activation`);
      }
      return InstallStatus.Installed;
    } catch (err) {
      try {
        await this.uninstall();
      } catch {
        // if a file is locked, it may not get removed. We'll deal with this later.
      }

      throw err;
    }
  }

  async writeManifest() {
    await this.targetLocation.createDirectory();
    await this.metadata.save(this.targetLocation.join('artifact.json'));
  }

  async uninstall() {
    await this.targetLocation.delete({ recursive: true, useTrash: false });
  }

  async loadActivationSettings(events: Partial<InstallEvents>) {
    // construct paths (bin, lib, include, etc.)
    // construct tools
    // compose variables
    // defines

    // record all the files in the artifact
    this.allPaths = (await this.targetLocation.readDirectory(undefined, { recursive: true })).select(([name, stat]) => stat === FileType.Directory ? name.fsPath + '/' : name.fsPath);
    for (const exportsBlock of this.applicableDemands.exports) {
      this.session.activation.addExports(exportsBlock, this.targetLocation);
    }

    // if espressif install
    if (this.metadata.espidf) {
      // check for some file that espressif installs to see if it's installed.
      if (!await this.targetLocation.exists('.espressif')) {
        await installEspIdf(this.session, events, this.targetLocation);
      }

      // activate
      await activateEspIdf(this.session, this.targetLocation);
    }

    return true;
  }

  async sanitizeAndValidatePath(path: string) {
    try {
      const loc = this.session.fileSystem.file(resolve(this.targetLocation.fsPath, path));
      if (await loc.exists()) {
        return loc;
      }
    } catch {
      // no worries, treat it like a relative path.
    }
    const loc = this.targetLocation.join(sanitizePath(path));
    if (await loc.exists()) {
      return loc;
    }
    return undefined;
  }
}

export function sanitizePath(path: string) {
  return path.
    replace(/[\\/]+/g, '/').     // forward slashes please
    replace(/[?<>:|"]/g, ''). // remove illegal characters.
    // eslint-disable-next-line no-control-regex
    replace(/[\x00-\x1f\x80-\x9f]/g, ''). // remove unicode control codes
    replace(/^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i, ''). // no reserved names
    replace(/^[/.]*\//, ''). // dots and slashes off the front.
    replace(/[/.]+$/, ''). // dots and slashes off the back.
    replace(/\/\.+\//g, '/'). // no parts made just of dots.
    replace(/\/+/g, '/'); // duplicate slashes.
}

export function sanitizeUri(u: string) {
  return u.
    replace(/[\\/]+/g, '/').     // forward slashes please
    replace(/[?<>|"]/g, ''). // remove illegal characters.
    // eslint-disable-next-line no-control-regex
    replace(/[\x00-\x1f\x80-\x9f]/g, ''). // remove unicode control codes
    replace(/^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i, ''). // no reserved names
    replace(/^[/.]*\//, ''). // dots and slashes off the front.
    replace(/[/.]+$/, ''). // dots and slashes off the back.
    replace(/\/\.+\//g, '/'). // no parts made just of dots.
    replace(/\/+/g, '/'); // duplicate slashes.
}

export class ProjectManifest extends ArtifactBase {
}

export class InstalledArtifact extends Artifact {
  constructor(session: Session, metadata: MetadataFile) {
    super(session, metadata, '', Uri.invalid);
  }
}

export interface ResolvedArtifact {
  artifact: ArtifactBase,
  uniqueId: string,
  initialSelection: boolean,
  depth: number,
  priority: number
}

export async function resolveDependencies(session: Session, registryResolver: RegistryResolver, initialParents: Array<ArtifactBase>, dependencyDepth: number): Promise<Array<ResolvedArtifact>> {
  let depth = 0;
  let nextDepthRegistries: Array<Registry | undefined> = initialParents.map((parent) =>
    parent.metadata.registryUri ? registryResolver.getRegistryByUri(parent.metadata.registryUri) : undefined);
  let currentRegistries: Array<Registry | undefined> = [];
  let nextDepth: Array<ArtifactBase> = initialParents;
  let initialSelections = new Set<string>();
  let current: Array<ArtifactBase> = [];
  let resultSet = new Map<string, ArtifactBase>(); // uniqueId, artifact
  let orderer = new Map<string, [number, number]>(); // uniqueId, [depth, priority]

  while (nextDepth.length !== 0) {
    ++depth;
    currentRegistries = nextDepthRegistries;
    nextDepthRegistries = [];
    current = nextDepth;
    nextDepth = [];

    if (depth == dependencyDepth) {
      initialSelections = new Set<string>(resultSet.keys());
    }

    for (let idx = 0; idx < current.length; ++idx) {
      const subjectParentRegistry = currentRegistries[idx];
      const subject = current[idx];
      let subjectId: string;
      let subjectUniqueId: string;
      if (subject instanceof Artifact) {
        subjectId = subject.id;
        subjectUniqueId = subject.uniqueId;
      } else {
        subjectId = subject.metadata.file.toString();
        subjectUniqueId = subjectId;
      }

      session.channels.debug(`Resolving ${subjectUniqueId}'s dependencies...`);
      // Note that we must update depth even if visiting the same artifact again
      orderer.set(subjectUniqueId, [depth, subject.metadata.priority]);
      if (resultSet.has(subjectUniqueId)) {
        session.channels.debug(`${subjectUniqueId} is a terminal dependency with a depth of ${depth}.`);
        // already visited
        continue;
      }

      resultSet.set(subjectUniqueId, subject);
      for (const [idOrShortName, version] of linq.entries(subject.applicableDemands.requires)) {
        const [dependencyRegistryDeclaredName, dependencyId] = parseArtifactDependency(idOrShortName);
        let dependencyRegistry: Registry;
        if (dependencyRegistryDeclaredName) {
          const maybeRegistry = (await subject.buildRegistryResolver()).getRegistryByName(dependencyRegistryDeclaredName);
          if (!maybeRegistry) {
            throw new Error(i`While resolving dependencies of ${subjectId}, ${dependencyRegistryDeclaredName} in ${idOrShortName} could not be resolved to a registry.`);
          }

          dependencyRegistry = maybeRegistry;
        } else {
          if (!subjectParentRegistry) {
            throw new Error(i`While resolving dependencies of the project file ${subjectId}, ${idOrShortName} did not specify a registry.`);
          }

          dependencyRegistry = subjectParentRegistry;
        }

        const dependencyRegistryDisplayName = registryResolver.getRegistryDisplayName(dependencyRegistry.location);
        session.channels.debug(`Interpreting '${idOrShortName}' as ${dependencyRegistry.location.toString()}:${dependencyId}`);
        const dependency = await getArtifact(dependencyRegistry, dependencyId, version.raw);
        if (!dependency) {
          throw new Error(i`Unable to resolve dependency ${dependencyId} in ${prettyRegistryName(dependencyRegistryDisplayName)}.`);
        }

        session.channels.debug(`Resolved dependency ${artifactIdentity(dependencyRegistryDisplayName, dependency[0], dependency[1].shortName)}`);
        nextDepthRegistries.push(dependencyRegistry);
        nextDepth.push(dependency[1]);
      }
    }
  }

  if (initialSelections.size === 0) {
    initialSelections = new Set<string>(resultSet.keys());
  }

  session.channels.debug(`The following are initial selections: ${Array.from(initialSelections).join(', ')}`);

  const results = new Array<ResolvedArtifact>();
  for (const [uniqueId, artifact] of resultSet) {
    const order = orderer.get(uniqueId);
    if (order) {
      results.push({
        'artifact': artifact,
        'uniqueId': uniqueId,
        'initialSelection': initialSelections.has(uniqueId),
        'depth': order[0],
        'priority': artifact.metadata.priority
      });
    } else {
      throw new Error('Result artifact with no order (bug in resolveDependencies)');
    }
  }

  results.sort((a, b) => {
    if (a.depth != b.depth) {
      return b.depth - a.depth;
    }

    return a.priority - b.priority;
  });

  return results;
}

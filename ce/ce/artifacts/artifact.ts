/* eslint-disable prefer-const */
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { match } from 'micromatch';
import { delimiter, resolve } from 'path';
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
      result.add(await session.registryDatabase.loadRegistry(session, loc), name);
    }
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

  async install(events: Partial<InstallEvents>, options: { force?: boolean, allLanguages?: boolean, language?: string }): Promise<boolean> {
    let installing = false;
    try {
      // is it installed?
      const applicableDemands = this.applicableDemands;

      this.session.channels.error(applicableDemands.errors, this);

      if (applicableDemands.errors.length) {
        throw Error('Error message from Artifact');
      }

      this.session.channels.warning(applicableDemands.warnings, this);
      this.session.channels.message(applicableDemands.messages, this);

      if (await this.isInstalled && !options.force) {
        if (!await this.loadActivationSettings(events)) {
          throw new Error(i`Failed during artifact activation`);
        }
        return false;
      }
      installing = true;

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
      return true;
    } catch (err) {
      if (installing) {
        // if we started installing, and then had an error, we need to remove the artifact.
        try {
          await this.uninstall();
        } catch {
          // if a file is locked, it may not get removed. We'll deal with this later.
        }
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

  matchFilesInArtifact(glob: string) {
    const results = match(this.allPaths, glob.trim(), { dot: true, cwd: this.targetLocation.fsPath, unescape: true });
    if (results.length === 0) {
      this.session.channels.warning(i`Unable to resolve '${glob}' to files in the artifact folder`, this);
      return [];
    }
    return results;
  }

  resolveBraces(text: string, mustBeSingle = false) {
    return text.replace(/\{(.*?)\}/g, (m, e) => {
      const results = this.matchFilesInArtifact(e);
      if (mustBeSingle && results.length > 1) {
        this.session.channels.warning(i`Glob ${m} resolved to multiple locations. Using first location.`, this);
        return results[0];
      }
      return results.join(delimiter);
    });
  }

  resolveBracesAndSplit(text: string): Array<string> {
    return this.resolveBraces(text).split(delimiter);
  }

  isGlob(path: string) {
    return path.indexOf('*') !== -1 || path.indexOf('?') !== -1;
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

export async function resolveRegistries(registryResolver: RegistryResolver, initialParents: Array<ArtifactBase>): Promise<Array<Registry | undefined>> {
  const result: Array<Registry | undefined> = [];
  for (const parent of initialParents) {
    let registry: Registry | undefined;
    const maybeRegistryUri = parent.metadata.registryUri;
    if (maybeRegistryUri) {
      registry = registryResolver.getRegistryByUri(maybeRegistryUri);
    }

    result.push(registry);
  }

  return result;
}

export async function resolveDependencies(session: Session, registryResolver: RegistryResolver, initialParents: Array<ArtifactBase>, dependencyDepth: number): Promise<Array<ResolvedArtifact>> {
  let depth = 0;
  let nextDepthParents: Array<Registry | undefined> = await resolveRegistries(registryResolver, initialParents);
  let currentParents: Array<Registry | undefined> = [];
  let nextDepth: Array<ArtifactBase> = initialParents;
  let initialSelections = new Set<string>();
  let current: Array<ArtifactBase> = [];
  let resultSet = new Map<string, ArtifactBase>(); // uniqueId, artifact
  let orderer = new Map<string, [number, number]>(); // uniqueId, [depth, priority]

  while (nextDepth.length !== 0) {
    ++depth;
    currentParents = nextDepthParents;
    nextDepthParents = [];
    current = nextDepth;
    nextDepth = [];

    if (depth == dependencyDepth) {
      initialSelections = new Set<string>(resultSet.keys());
    }

    for (let idx = 0; idx < current.length; ++idx) {
      const subjectParentRegistry = currentParents[idx];
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

        session.channels.debug(`Resolved dependency ${artifactIdentity(dependencyRegistryDisplayName, dependency[0])}`);
        nextDepthParents.push(dependencyRegistry);
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
        'priority': order[1]
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

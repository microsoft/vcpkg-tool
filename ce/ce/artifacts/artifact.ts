/* eslint-disable prefer-const */
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail } from 'assert';
import { match } from 'micromatch';
import { delimiter, resolve } from 'path';
import { MetadataFile } from '../amf/metadata-file';
import { latestVersion } from '../constants';
import { FileType } from '../fs/filesystem';
import { i } from '../i18n';
import { activateEspIdf, installEspIdf } from '../installers/espidf';
import { InstallEvents } from '../interfaces/events';
import { Registries } from '../registries/registries';
import { Session } from '../session';
import { linq } from '../util/linq';
import { Uri } from '../util/uri';
import { Registry } from './registry';
import { SetOfDemands } from './SetOfDemands';


export type Selections = Map<string, string>;
export type UID = string;
export type ID = string;
export type VersionRange = string;
export type Selection = [Artifact, ID, VersionRange]

export class ArtifactMap extends Map<UID, Selection>{
  get artifacts() {
    return Array.from([...linq.values(this).select(([artifact, id, range]) => artifact)].sort((a, b) => b.metadata.priority - a.metadata.priority));
  }
}

class ArtifactBase {
  public registries: Registries;
  readonly applicableDemands: SetOfDemands;

  constructor(protected session: Session, public readonly metadata: MetadataFile) {
    this.applicableDemands = new SetOfDemands(this.metadata, this.session);
    this.registries = new Registries(session);
  }

  /** Async Initializer */
  async init(session: Session) {
    await this.applicableDemands.init(session);

    // load the registries from the project file
    for (const [name, registry] of this.metadata.registries) {
      const loc = registry.location.get(0);
      if (loc) {
        this.registries.add(await session.loadRegistry(loc), name);
      }
    }

    return this;
  }

  async resolveDependencies(artifacts = new ArtifactMap(), recurse = true) {
    // find the dependencies and add them to the set
    let dependency: [Registry, string, Artifact] | undefined;
    for (const [id, version] of linq.entries(this.applicableDemands.requires)) {
      dependency = undefined;
      if (id.indexOf(':') === -1) {
        if (this.metadata.registry) {
          // let's assume the dependency is in the same registry as the artifact
          const [registry, b, artifacts] = (await this.metadata.registry.search(this.registries, { idOrShortName: id, version: version.raw }))[0];
          dependency = [registry, b, artifacts[0]];
          if (!dependency) {
            throw new Error(i`Dependency '${id}' version '${version.raw}' does not specify the registry.`);
          }
        }
      }
      dependency = dependency || await this.registries.getArtifact(id, version.raw);
      if (!dependency) {
        throw new Error(i`Unable to resolve dependency ${id}: ${version.raw}`);
      }
      const artifact = dependency[2];
      if (!artifacts.has(artifact.uniqueId)) {
        artifacts.set(artifact.uniqueId, [artifact, id, version.raw || latestVersion]);

        if (recurse) {
          // process it's dependencies too.
          await artifact.resolveDependencies(artifacts);
        }
      }
    }

    return artifacts;
  }
}

export class Artifact extends ArtifactBase {
  isPrimary = false;
  allPaths: Array<string> = [];

  constructor(session: Session, metadata: MetadataFile, public shortName: string = '', public targetLocation: Uri, public readonly registryUri: Uri) {
    super(session, metadata);
  }

  get id() {
    return this.metadata.id;
  }

  get version() {
    return this.metadata.version;
  }

  get isInstalled() {
    return this.targetLocation.exists('artifact.yaml');
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
    await this.metadata.save(this.targetLocation.join('artifact.yaml'));
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
    super(session, metadata, '', Uri.invalid, Uri.invalid);
  }
}

export function parseArtifactName(id: string): [string | undefined, string] {
  const parts = id.split(':');
  if (parts.length === 2) {
    return [parts[0], parts[1]];
  }

  if (parts.length === 1) {
    return [undefined, parts[0]];
  }

  throw new Error(i`Invalid artifact id '${id}'`);
}

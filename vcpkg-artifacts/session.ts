// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { createHash } from 'crypto';
import { MetadataFile } from './amf/metadata-file';
import { Artifact, InstalledArtifact } from './artifacts/artifact';
import { configurationName, defaultConfig } from './constants';
import { FileSystem } from './fs/filesystem';
import { LocalFileSystem } from './fs/local-filesystem';
import { UnifiedFileSystem } from './fs/unified-filesystem';
import { VsixLocalFilesystem } from './fs/vsix-local-filesystem';
import { i } from './i18n';
import { installGit } from './installers/git';
import { installNuGet } from './installers/nuget';
import { installUnTar } from './installers/untar';
import { installUnZip } from './installers/unzip';
import { InstallEvents, InstallOptions } from './interfaces/events';
import { Installer } from './interfaces/metadata/installers/Installer';
import { RegistryDatabase, RegistryResolver } from './registries/registries';
import { Channels, Stopwatch } from './util/channels';
import { Uri } from './util/uri';
import { HttpsFileSystem } from './fs/http-filesystem';

/** The definition for an installer tool function */
type InstallerTool<T extends Installer = any> = (
  session: Session,
  name: string,
  version: string,
  targetLocation: Uri,
  install: T,
  events: Partial<InstallEvents>,
  options: Partial<InstallOptions>
) => Promise<void>


export type Context = { [key: string]: Array<string> | undefined; } & {
  readonly os: string;
  readonly arch: string;
  readonly windows: boolean;
  readonly osx: boolean;
  readonly linux: boolean;
  readonly freebsd: boolean;
  readonly x64: boolean;
  readonly x86: boolean;
  readonly arm: boolean;
  readonly arm64: boolean;
}

export type SessionSettings = {
  readonly vcpkgCommand?: string;
  readonly homeFolder: string;
  readonly vcpkgArtifactsRoot?: string;
  readonly vcpkgDownloads?: string;
  readonly vcpkgRegistriesCache?: string;
  readonly telemetryFile?: string;
  readonly nextPreviousEnvironment?: string;
  readonly globalConfig?: string;
}

interface ArtifactEntry {
  registryUri: string;
  id: string;
  version: string;
}

function hexsha(content: string) {
  return createHash('sha256').update(content, 'ascii').digest('hex');
}

function formatArtifactEntry(entry: ArtifactEntry): string {
  // we hash all the things to remove PII
  return `${hexsha(entry.registryUri)}:${hexsha(entry.id)}:${hexsha(entry.version)}`;
}

/**
 * The Session class is used to hold a reference to the
 * message channels,
 * the filesystems,
 * and any other 'global' data that should be kept.
 *
 */
export class Session {
  /** @internal */
  readonly stopwatch = new Stopwatch();
  readonly fileSystem: FileSystem;
  readonly channels: Channels;
  readonly homeFolder: Uri;
  readonly nextPreviousEnvironment: Uri;
  readonly installFolder: Uri;
  readonly registryFolder: Uri;
  readonly telemetryFile: Uri | undefined;
  get vcpkgCommand() { return this.settings.vcpkgCommand; }

  readonly globalConfig: Uri;
  readonly downloads: Uri;
  currentDirectory: Uri;
  configuration?: MetadataFile;

  /** register installer functions here */
  private installers = new Map<string, InstallerTool>([
    ['nuget', installNuGet],
    ['unzip', installUnZip],
    ['untar', installUnTar],
    ['git', installGit]
  ]);

  readonly registryDatabase = new RegistryDatabase();
  readonly globalRegistryResolver = new RegistryResolver(this.registryDatabase);

  processVcpkgArg(argSetting: string | undefined, defaultName: string): Uri {
    return argSetting ? this.fileSystem.file(argSetting) : this.homeFolder.join(defaultName);
  }

  constructor(currentDirectory: string, public readonly context: Context, public readonly settings: SessionSettings) {
    this.fileSystem = new UnifiedFileSystem(this).
      register('file', new LocalFileSystem(this)).
      register('vsix', new VsixLocalFilesystem(this)).
      register('https', new HttpsFileSystem(this));

    this.channels = new Channels(this);

    if (settings.telemetryFile) {
      this.telemetryFile = this.fileSystem.file(settings.telemetryFile);
    }

    this.homeFolder = this.fileSystem.file(settings.homeFolder);
    this.downloads = this.processVcpkgArg(settings.vcpkgDownloads, 'downloads');
    this.globalConfig = this.processVcpkgArg(settings.globalConfig, configurationName);

    this.registryFolder = this.processVcpkgArg(settings.vcpkgRegistriesCache, 'registries').join('artifact');
    this.installFolder = this.processVcpkgArg(settings.vcpkgArtifactsRoot, 'artifacts');
    this.nextPreviousEnvironment = this.processVcpkgArg(settings.nextPreviousEnvironment, `previous-environment-${Date.now().toFixed()}.json`);

    this.currentDirectory = this.fileSystem.file(currentDirectory);
  }

  parseLocation(location: string): Uri {
    // Drive letter, absolute Unix path, or drive-relative windows path, treat as a file
    if (/^[A-Za-z]:/.exec(location) || location.startsWith('/') || location.startsWith('\\')) {
      return this.fileSystem.file(location);
    }

    // Otherwise, it's a URI
    return this.fileSystem.parseUri(location);
  }

  async saveConfig() {
    await this.configuration?.save(this.globalConfig);
  }

  async init() {
    // load global configuration
    if (!await this.fileSystem.isDirectory(this.homeFolder)) {
      // let's create the folder
      try {
        await this.fileSystem.createDirectory(this.homeFolder);
      } catch (error: any) {
        // if this throws, let it
        this.channels.debug(error?.message);
      }
      // check if it got made, because at an absolute minimum, we need a folder, so failing this is catastrophic.
      strict.ok(await this.fileSystem.isDirectory(this.homeFolder), i`Fatal: The root folder '${this.homeFolder.fsPath}' cannot be created`);
    }

    if (!await this.fileSystem.isFile(this.globalConfig)) {
      try {
        await this.globalConfig.writeUTF8(defaultConfig);
      } catch {
        // if this throws, let it
      }
      // check if it got made, because at an absolute minimum, we need the config file, so failing this is catastrophic.
      strict.ok(await this.fileSystem.isFile(this.globalConfig), i`Fatal: The global configuration file '${this.globalConfig.fsPath}' cannot be created`);
    }

    // got past the checks, let's load the configuration.
    this.configuration = await MetadataFile.parseMetadata(this.globalConfig.fsPath, this.globalConfig, this);
    this.channels.debug(`Loaded global configuration file '${this.globalConfig.fsPath}'`);

    // load the registries
    for (const [name, regDef] of this.configuration.registries) {
      const loc = regDef.location.get(0);
      if (loc) {
        const uri = this.parseLocation(loc);
        const reg = await this.registryDatabase.loadRegistry(this, uri);
        this.globalRegistryResolver.add(uri, name);
        if (reg) {
          this.channels.debug(`Loaded global manifest ${name} => ${uri.formatted}`);
        }
      }
    }

    return this;
  }

  async findProjectProfile(startLocation = this.currentDirectory): Promise<Uri | undefined> {
    let location = startLocation;
    const path = location.join(configurationName);
    if (await this.fileSystem.isFile(path)) {
      return path;
    }

    location = location.join('..');
    return (location.toString() === startLocation.toString()) ? undefined : this.findProjectProfile(location);
  }

  async getInstalledArtifacts() {
    const result = new Array<{ folder: Uri, id: string, artifact: Artifact }>();
    if (! await this.installFolder.exists()) {
      return result;
    }
    for (const [folder] of await this.installFolder.readDirectory(undefined, { recursive: true })) {
      try {
        const artifactJsonPath = folder.join('artifact.json');
        const metadata = await MetadataFile.parseMetadata(artifactJsonPath.fsPath, artifactJsonPath, this);
        result.push({
          folder,
          id: metadata.id,
          artifact: await new InstalledArtifact(this, metadata)
        });
      } catch {
        // not a valid install.
      }
    }
    return result;
  }

  /** returns an installer function (or undefined) for a given installerkind */
  artifactInstaller(installInfo: Installer) {
    return this.installers.get(installInfo.installerKind);
  }

  async openManifest(filename: string, uri: Uri): Promise<MetadataFile> {
    return await MetadataFile.parseConfiguration(filename, await uri.readUTF8(), this);
  }

  readonly #acquiredArtifacts: Array<ArtifactEntry> = [];
  readonly #activatedArtifacts: Array<ArtifactEntry> = [];

  trackAcquire(registryUri: string, id: string, version: string) {
    this.#acquiredArtifacts.push({ registryUri: registryUri, id: id, version: version });
  }

  trackActivate(registryUri: string, id: string, version: string) {
    this.#activatedArtifacts.push({ registryUri: registryUri, id: id, version: version });
  }

  writeTelemetry(): Promise<any> {
    const acquiredArtifacts = this.#acquiredArtifacts.map(formatArtifactEntry).join(',');
    const activatedArtifacts = this.#activatedArtifacts.map(formatArtifactEntry).join(',');

    const telemetryFile = this.telemetryFile;
    if (telemetryFile) {
      return telemetryFile.writeUTF8(JSON.stringify({
        'acquired-artifacts': acquiredArtifacts,
        'activated-artifacts': activatedArtifacts
      }));
    }

    return Promise.resolve(undefined);
  }
}

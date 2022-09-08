// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { MetadataFile } from './amf/metadata-file';
import { Activation, deactivate } from './artifacts/activation';
import { Artifact, InstalledArtifact, ProjectManifest } from './artifacts/artifact';
import { configurationName, defaultConfig, globalConfigurationFile, postscriptVariable, undo } from './constants';
import { FileSystem } from './fs/filesystem';
import { HttpsFileSystem } from './fs/http-filesystem';
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
  readonly homeFolder?: string;
  readonly vcpkgArtifactsRoot?: string;
  readonly vcpkgDownloads?: string;
  readonly vcpkgRegistriesCache?: string;
  readonly telemetryEnabled: boolean;
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
  readonly tmpFolder: Uri;
  readonly installFolder: Uri;
  readonly registryFolder: Uri;
  readonly activation: Activation = new Activation(this);
  get vcpkgCommand() { return this.settings.vcpkgCommand; }

  readonly globalConfig: Uri;
  readonly downloads: Uri;
  readonly telemetryEnabled: boolean;
  currentDirectory: Uri;
  configuration!: MetadataFile;

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

  constructor(currentDirectory: string, public readonly context: Context, public readonly settings: SessionSettings, public readonly environment: NodeJS.ProcessEnv) {
    this.fileSystem = new UnifiedFileSystem(this).
      register('file', new LocalFileSystem(this)).
      register('vsix', new VsixLocalFilesystem(this)).
      register(['https'], new HttpsFileSystem(this)
      );

    this.channels = new Channels(this);

    this.telemetryEnabled = this.settings['telemetryEnabled'];

    this.homeFolder = this.fileSystem.file(settings.homeFolder!);
    this.downloads = this.processVcpkgArg(settings.vcpkgDownloads, 'downloads');
    this.globalConfig = this.homeFolder.join(globalConfigurationFile);

    this.tmpFolder = this.homeFolder.join('tmp');

    this.registryFolder = this.processVcpkgArg(settings.vcpkgRegistriesCache, 'registries').join('artifact');
    this.installFolder = this.processVcpkgArg(settings.vcpkgArtifactsRoot, 'artifacts');

    this.currentDirectory = this.fileSystem.file(currentDirectory);
  }

  parseLocation(location: string): Uri {
    // Drive letter, absolute Unix path, or drive-relative windows path, treat as a file
    if (/^[A-Za-z]:/.exec(location) || location.startsWith('/') || location.startsWith('\\')) {
      return this.fileSystem.file(location);
    }

    // Has a scheme, it's a URI
    if (/^(\w+):/.exec(location)) {
      return this.fileSystem.parse(location);
    }

    // Relative path
    return this.currentDirectory.join(location);
  }

  async loadDefaultRegistryResolver(manifest: ProjectManifest | undefined) : Promise<RegistryResolver> {
    const manifestResolver = await manifest?.buildRegistryResolver();
    if (manifestResolver) {
      this.channels.debug('Combining project manifests with global ones.');
      return manifestResolver.combineWith(this.globalRegistryResolver);
    }

    return this.globalRegistryResolver;
  }

  async saveConfig() {
    await this.configuration.save(this.globalConfig);
  }

  #postscriptFile?: Uri;
  get postscriptFile() {
    return this.#postscriptFile || (this.#postscriptFile = this.environment[postscriptVariable] ? this.fileSystem.file(this.environment[postscriptVariable]!) : undefined);
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
      strict.ok(await this.fileSystem.isDirectory(this.homeFolder), i`Fatal: The root folder '${this.homeFolder.fsPath}' can not be created`);
    }

    if (!await this.fileSystem.isFile(this.globalConfig)) {
      try {
        await this.globalConfig.writeUTF8(defaultConfig);
      } catch {
        // if this throws, let it
      }
      // check if it got made, because at an absolute minimum, we need the config file, so failing this is catastrophic.
      strict.ok(await this.fileSystem.isFile(this.globalConfig), i`Fatal: The global configuration file '${this.globalConfig.fsPath}' can not be created`);
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

  async findProjectProfile(startLocation = this.currentDirectory, search = true): Promise<Uri | undefined> {
    let location = startLocation;
    const path = location.join(configurationName);
    if (await this.fileSystem.isFile(path)) {
      return path;
    }
    location = location.join('..');
    if (search) {
      return (location.toString() === startLocation.toString()) ? undefined : this.findProjectProfile(location);
    }
    return undefined;
  }

  async deactivate() {
    const previous = this.environment[undo];
    if (previous && this.postscriptFile) {
      const deactivationDataFile = this.fileSystem.file(previous);
      if (deactivationDataFile.scheme === 'file' && await deactivationDataFile.exists()) {

        const deactivationData = JSON.parse(await deactivationDataFile.readUTF8());
        delete deactivationData.environment[undo];
        await deactivate(this.postscriptFile, deactivationData.environment || {}, deactivationData.aliases || {});
        await deactivationDataFile.delete();
      }
    }
  }

  async getInstalledArtifacts() {
    const result = new Array<{ folder: Uri, id: string, artifact: Artifact }>();
    if (! await this.installFolder.exists()) {
      return result;
    }
    for (const [folder, stat] of await this.installFolder.readDirectory(undefined, { recursive: true })) {
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

  async openManifest(manifestFile: Uri): Promise<MetadataFile> {
    return await MetadataFile.parseConfiguration(manifestFile.fsPath, await manifestFile.readUTF8(), this);
  }

  serializer(key: any, value: any) {
    if (value instanceof Map) {
      return { dataType: 'Map', value: Array.from(value.entries()) };
    }
    return value;
  }

  deserializer(key: any, value: any) {
    if (typeof value === 'object' && value !== null) {
      switch (value.dataType) {
        case 'Map':
          return new Map(value.value);
      }
      if (value.scheme && value.path) {
        return this.fileSystem.from(value);
      }
    }
    return value;
  }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isSeq } from 'yaml';
import { GitInstaller } from '../interfaces/metadata/installers/git';
import { Installer as IInstaller } from '../interfaces/metadata/installers/Installer';
import { NupkgInstaller } from '../interfaces/metadata/installers/nupkg';
import { UnTarInstaller } from '../interfaces/metadata/installers/tar';
import { UnZipInstaller } from '../interfaces/metadata/installers/zip';
import { ValidationMessage } from '../interfaces/validation-message';
import { Entity } from '../yaml/Entity';
import { EntitySequence } from '../yaml/EntitySequence';
import { Options } from '../yaml/Options';
import { Strings } from '../yaml/strings';
import { Node, Yaml, YAMLDictionary } from '../yaml/yaml-types';

export class Installs extends EntitySequence<Installer> {
  constructor(node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(Installer, node, parent, key);
  }

  override *[Symbol.iterator](): Iterator<Installer> {
    if (isMap(this.node)) {
      yield this.createInstance(this.node);
    }
    if (isSeq(this.node)) {
      for (const item of this.node.items) {
        yield this.createInstance(item);
      }
    }
  }

  protected createInstance(node: Node): Installer {
    if (isMap(node)) {
      if (node.has('unzip')) {
        return new UnzipNode(node, this);
      }
      if (node.has('nupkg')) {
        return new NupkgNode(node, this);
      }
      if (node.has('untar')) {
        return new UnTarNode(node, this);
      }
      if (node.has('git')) {
        return new GitCloneNode(node, this);
      }
    }
    throw new Error('Unsupported node type');
  }

  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    for (const each of this) {
      yield* each.validate();
    }
  }
}

export class Installer extends Entity implements IInstaller {
  get installerKind(): string {
    throw new Error('abstract type, should not get here.');
  }

  override get fullName(): string {
    return `${super.fullName}.${this.installerKind}`;
  }

  get lang() {
    return this.asString(this.getMember('lang'));
  }

  get nametag() {
    return this.asString(this.getMember('nametag'));
  }

  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChild('lang', 'string');
    yield* this.validateChild('nametag', 'string');
  }
}

abstract class FileInstallerNode extends Installer {
  get sha256() {
    return this.asString(this.getMember('sha256'));
  }

  set sha256(value: string | undefined) {
    this.setMember('sha256', value);
  }

  get sha512() {
    return this.asString(this.getMember('sha512'));
  }

  set sha512(value: string | undefined) {
    this.setMember('sha512', value);
  }

  get strip() {
    return this.asNumber(this.getMember('strip'));
  }

  set strip(value: number | undefined) {
    this.setMember('1', value);
  }

  readonly transform = new Strings(undefined, this, 'transform');

  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChild('strip', 'number');
    yield* this.validateChild('sha256', 'string');
    yield* this.validateChild('sha512', 'string');
  }

}
class UnzipNode extends FileInstallerNode implements UnZipInstaller {
  override get installerKind() { return 'unzip'; }

  readonly location = new Strings(undefined, this, 'unzip');
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['unzip', 'sha256', 'sha512', 'strip', 'transform', 'lang', 'nametag']);
  }

}
class UnTarNode extends FileInstallerNode implements UnTarInstaller {
  override get installerKind() { return 'untar'; }
  location = new Strings(undefined, this, 'untar');
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['untar', 'sha256', 'sha512', 'strip', 'transform']);
  }
}
class NupkgNode extends Installer implements NupkgInstaller {
  get location() {
    return this.asString(this.getMember('nupkg'))!;
  }

  set location(value: string) {
    this.setMember('nupkg', value);
  }

  override get installerKind() { return 'nupkg'; }

  get strip() {
    return this.asNumber(this.getMember('strip'));
  }

  set strip(value: number | undefined) {
    this.setMember('1', value);
  }

  get sha256() {
    return this.asString(this.getMember('sha256'));
  }

  set sha256(value: string | undefined) {
    this.setMember('sha256', value);
  }

  get sha512() {
    return this.asString(this.getMember('sha512'));
  }

  set sha512(value: string | undefined) {
    this.setMember('sha512', value);
  }

  readonly transform = new Strings(undefined, this, 'transform');
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['nupkg', 'sha256', 'sha512', 'strip', 'transform', 'lang', 'nametag']);
  }

}
class GitCloneNode extends Installer implements GitInstaller {
  override get installerKind() { return 'git'; }

  get location() {
    return this.asString(this.getMember('git'))!;
  }

  set location(value: string) {
    this.setMember('git', value);
  }

  get commit() {
    return this.asString(this.getMember('commit'));
  }

  set commit(value: string | undefined) {
    this.setMember('commit', value);
  }

  private options = new Options(undefined, this, 'options');

  get full() {
    return this.options.has('full');
  }

  set full(value: boolean) {
    this.options.set('full', value);
  }

  get recurse() {
    return this.options.has('recurse');
  }

  set recurse(value: boolean) {
    this.options.set('recurse', value);
  }

  get subdirectory() {
    return this.asString(this.getMember('subdirectory'));
  }

  set subdirectory(value: string | undefined) {
    this.setMember('subdirectory', value);
  }

  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['git', 'commit', 'subdirectory', 'options', 'lang', 'nametag']);
    yield* this.validateChild('commit', 'string');
  }
}

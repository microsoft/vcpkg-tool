// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { Activation } from '../../artifacts/activation';
import { Session } from '../../session';
import { Dictionary, Sequence } from '../collections';
import { Validation } from '../validation';
import { AlternativeFulfillment } from './alternative-fulfillment';
import { Installer } from './installers/Installer';
import { Settings } from './Settings';
import { VersionReference } from './version-reference';

/**
 * These are the things that are necessary to install/set/depend-on/etc for a given 'artifact'
 */

export interface Demands extends Validation {
  /** set of required artifacts */
  requires: Dictionary<VersionReference>;

  /** An error message that the user should get, and abort the installation */
  error: string | undefined; // markdown text with ${} replacements


  /** A warning message that the user should get, does not abort the installation */
  warning: string | undefined; // markdown text with ${} replacements


  /** A text message that the user should get, does not abort the installation */
  message: string | undefined; // markdown text with ${} replacements


  /** set of artifacts that the consumer should be aware of */
  seeAlso: Dictionary<VersionReference>;

  /** settings that should be applied to the context when activated */
  settings: Settings;

  /**
   * defines what should be physically laid out on disk for this artifact
   *
   * Note: once the host/environment queries have been completed, there should
   *       only be one single package/file/repo/etc that gets downloaded and
   *       installed for this artifact.  If there needs to be more than one,
   *       then there would need to be a 'requires' that refers to the additional
   *       package.
   */
  install: Sequence<Installer>;

  /** a means to an alternative fulfillment */
  unless: AlternativeFulfillment;

  init(session: Session): Promise<Demands>;
  setActivation(activation: Activation): void;
}


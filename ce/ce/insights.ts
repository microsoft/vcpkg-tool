// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { defaultClient, DistributedTracingModes, setup } from 'applicationinsights';
import { createHash } from 'crypto';
import { session } from './main';
import { Version } from './version';

process.env['APPLICATION_INSIGHTS_NO_STATSBEAT'] = 'true';
export const insights = setup('b4e88960-4393-4dd9-ab8e-97e8fe6d7603').
  setAutoCollectConsole(false).
  setAutoCollectDependencies(false).
  setAutoCollectExceptions(false).
  setAutoCollectHeartbeat(false).
  setAutoCollectPerformance(false).
  setAutoCollectPreAggregatedMetrics(false).
  setAutoCollectRequests(false).
  setAutoDependencyCorrelation(false).
  setDistributedTracingMode(DistributedTracingModes.AI).
  setInternalLogging(false).
  setSendLiveMetrics(false).
  setUseDiskRetryCaching(false).
  start();

defaultClient.context.keys.applicationVersion = Version;


// todo: This will be refactored to allow appInsights to be called out-of-proc from the main process.
//       in order to not potentially slow down or block on activation/etc.

export function flushTelemetry() {
  session.channels.debug('Ensuring Telemetry data is finished sending.');
  defaultClient.flush({});
}

defaultClient.addTelemetryProcessor((envelope, contextObjects) => {
  if (session.context['printmetrics']) {
    session.channels.message(`Telemetry Event: \n${JSON.stringify(envelope.data, null, 2)}`);
  }

  // only actually send telemetry if it's enabled.
  return session.telemetryEnabled;
});

export function trackEvent(name: string, properties: { [key: string]: string } = {}) {
  session.channels.debug(`Triggering Telemetry Event ce.${name}`);

  defaultClient.trackEvent({
    name: `ce/${name}`,
    time: new Date(),

    properties: {
      ...properties,
    }
  });
}

export function trackActivation() {
  return trackEvent('activate', {});
}

export function trackAcquire(artifactId: string, artifactVersion: string) {
  return trackEvent('acquire', {
    'artifactId': createHash('sha256').update(artifactId, 'ascii').digest('hex'),
    'artifactVersion': artifactVersion
  });
}

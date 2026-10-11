// The page's door to C++, shared by every page. In the app, Qt WebEngine hands
// the page qt.webChannelTransport and WebHost injects qwebchannel.js before
// any page script runs; in a plain browser (npm run dev) or a test neither
// exists, and the caller's mock stands in.

declare global {
  interface Window {
    qt?: { webChannelTransport: unknown };
    QWebChannel?: new (
      transport: unknown,
      ready: (channel: { objects: Record<string, unknown> }) => void,
    ) => unknown;
  }
}

/** True when the page is hosted by the app (a web channel is on offer). */
export function hosted(): boolean {
  return !!(window.qt && window.QWebChannel);
}

/** Every object C++ registered on the channel, by name. */
export function channelObjects(): Promise<Record<string, unknown>> {
  return new Promise((resolve) => {
    new window.QWebChannel!(window.qt!.webChannelTransport, (channel) => resolve(channel.objects));
  });
}

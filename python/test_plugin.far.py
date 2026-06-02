"""
Simple test plugin for Far Manager - no ctypes dependency
"""

import logging

log = logging.getLogger(__name__)


class Plugin:
    """Simple test plugin class"""

    # Plugin metadata
    name = "Simple Test Plugin"
    title = "Simple Plugin"
    author = "Developer"
    description = "A simple Python plugin for Far Manager"
    version = (1, 0, 0, 0)

    # UUID for the plugin
    guid = "87654321-4321-4321-4321-210987654321"

    # Where the plugin appears
    openFrom = ["PLUGINSMENU", "COMMANDLINE"]

    def __init__(self, psi_ptr=None):
        """Initialize plugin with PluginStartupInfo pointer"""
        self.psi_ptr = psi_ptr
        log.info("Simple test plugin initialized")

    def get_plugin_info(self):
        """Return plugin metadata dict (required by the adapter)."""
        return {
            "title": self.title,
            "description": self.description,
            "author": self.author,
            "version": ".".join(str(v) for v in self.version),
        }

    def GetPluginInfoW(self, info_ptr):
        """Provide plugin information to Far Manager"""
        log.debug("Simple plugin GetPluginInfoW called")
        # Don't modify C structures, just log
        return True

    def OpenW(self, info_ptr):
        """Called when plugin is opened from menu"""
        log.info("Simple plugin OpenW called")
        # Just log for now
        return 1

    def ConfigureW(self, info_ptr):
        """Called when plugin is configured"""
        log.info("Simple plugin ConfigureW called")
        return 1

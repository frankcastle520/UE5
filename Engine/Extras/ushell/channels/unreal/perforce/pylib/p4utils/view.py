import re

#-------------------------------------------------------------------------------
class _ViewWildcard(object):
    def __init__(self, view, *, allow_empty_wildcards=False, case_sensitive=False):
        self._src = view

        view = view.replace("\\", "/")

        if view.endswith("/"):
            view += "\x01"
        elif view.endswith("/..."):
            view = view[:-3] + "\x01"

        rooted = view.startswith("//")

        regex = ""
        sep = ""
        for piece in (x for x in view.split("/") if x):
            regex += sep
            sep = "/"

            if piece == "..." and allow_empty_wildcards:
                regex += "(?:[^/]+/)*"
                sep = ""
                continue

            piece = piece.replace("...", "\x01")
            piece = piece.replace("*", "\x02")
            piece = re.escape(piece)
            piece = piece.replace("\x01", ".*")
            piece = piece.replace("\x02", "[^/]*")
            regex += piece

        regex = "//" + regex if rooted else regex
        regex = (regex or "^") + "$"
        flags = None if case_sensitive else re.IGNORECASE
        self._re = re.compile(regex, flags)

    def get_view(self):
        return self._src

    def test(self, depot_path):
        return self._re.search(depot_path) is not None

#-------------------------------------------------------------------------------
class ViewFilter(object):
    class Query(object):
        def __init__(self, view_excludes):
            self._view_excludes = view_excludes

        def is_excluded(self, path):
            return next((x.get_view() for x in self._view_excludes if x.test(path)), False)

    def __init__(self):
        self._excluded_views = set()
        self._query = None

    def get_query(self):
        if self._query:
            return self._query
        ret = [_ViewWildcard(x, allow_empty_wildcards=True) for x in self._excluded_views]
        self._query = ViewFilter.Query(ret)
        return self._query

    def add_exclude(self, view):
        self._query = None
        self._excluded_views.add(view)

    def remove_exclude(self, view):
        try:
            self._excluded_views.remove(view)
            self._query = None
            return True
        except KeyError:
            return False

    def read_excludes(self):
        yield from self._excluded_views

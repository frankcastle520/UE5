# Copyright Epic Games, Inc. All Rights Reserved.

import datetime
import flow.cmd
import math
import multiprocessing.dummy
import os
import re
import subprocess
import sys
import time

p4_annotate_walltime = 0
p4_annotate_call_count = 0
p4_describe_walltime = 0
p4_describe_call_count = 0
p4_where_walltime = 0
p4_where_call_count = 0

def query_cl_linecount(file_path, print_error, print_warning, follow_integrations=False):
    start_time = time.time()

    follow_flag = "-i"
    if follow_integrations:
        follow_flag = "-I"

    # Run p4 annotate to get the CLs for each line.
    annotate_result = subprocess.run(["p4", "annotate", follow_flag, file_path], capture_output=True)
    annotate_stdout = annotate_result.stdout.decode("utf-8")
    annotate_stderr = annotate_result.stderr.decode("utf-8")
    if annotate_result.returncode != 0:
        msg = "p4 annotate failed, giving up. Output:\n%s\n%s" % (annotate_stdout, annotate_stderr)
        print_error(msg)
        sys.exit(1)

    global p4_annotate_walltime
    p4_annotate_walltime += time.time() - start_time
    global p4_annotate_call_count
    p4_annotate_call_count += 1

    # Give the user feedback if the annotated path cannot be found.
    if "no such file(s)" in annotate_stderr:
        print_error("p4 annotate call failed, giving up. Output:\n%s\n%s" % (annotate_stdout, annotate_stderr))
        sys.exit(1)

    annotate_lines = annotate_stdout.splitlines()

    # We count the number of lines changed by each CL.
    cl_linecount_map = {}

    # p4 annotate has a header followed by a output that looks like
    # "CL_NUMBER: LINE_CONTENT". Let's grab that CL_NUMBER with a regex!
    cl_regex = re.compile(r'^([0-9]+):')

    for line in annotate_lines:
        m = cl_regex.match(line)
        if not m:
            continue
        matching_string = m.group(1)
        changelist_number = int(matching_string)

        if changelist_number in cl_linecount_map:
            cl_linecount_map[changelist_number] += 1
        else:
            cl_linecount_map[changelist_number] = 1

    return cl_linecount_map

def query_owner_scores(cl_linecount_map, print_info, print_warning, print_error, start_date=None):
    owner_score_map = {}

    cl_count = len(cl_linecount_map)

    cl_process_list = []

    # Use at most 100 parallel calls to Perforce.
    pool = multiprocessing.dummy.Pool(100)

    def p4_describe(cl):
        describe_result = subprocess.run(["p4", "describe", "-s", str(cl)], capture_output=True)
        try:
            stdout_string = describe_result.stdout.decode("utf-8")
            return cl, stdout_string.splitlines()
        except UnicodeDecodeError:
            pass
        try:
            stdout_string = describe_result.stdout.decode("cp1252")
            return cl, stdout_string.splitlines()
        except UnicodeDecodeError:
            pass

        print_warning("Failed to decode output of \"p4 describe -s %d\". Results might be incorrect. Raw output was:" % cl, describe_result.stdout)

        return cl, []

    start_time = time.time()

    cl_lines_map = {}
    for cl, lines in pool.map(p4_describe, cl_linecount_map.keys()):
        cl_lines_map[cl] = lines
        global p4_describe_call_count
        p4_describe_call_count += 1

    global p4_describe_walltime
    p4_describe_walltime += time.time() - start_time

    # p4 describe has a header followed by the list of affected files formatted like
    # ... //P4/path/to/file.extension#REVISION action. Let's count those lines!
    changed_lines_regex = re.compile(r'^\.\.\.')

    # p4 describe mentiones the author as "Change 123 by Person.Name@Branch on date".
    # Let's grab the author's name from that!
    describe_owner_regex = re.compile(r'.* by (\S+)@\S+ on .*')

    # p4 describe prints the date of a changelist. Grab it!
    date_regex = re.compile(r'.* on (\d\d\d\d/\d\d/\d\d) .*')

    start_date_skip_count = 0

    for cl, linecount in cl_linecount_map.items():
        describe_lines = cl_lines_map[cl]

        owner = None
        date_found = False
        skip_cl = False
        match_count = 0

        for line in describe_lines:
            if start_date and not date_found and not skip_cl:
                date_match = date_regex.match(line)
                if date_match:
                    date_string = date_match.group(1)
                    date = datetime.datetime.strptime(date_string, "%Y/%m/%d")
                    date_found = True

                    # Skip this CL if it's dated before our start date.
                    if date < start_date:
                        start_date_skip_count += 1
                        skip_cl = True

            if changed_lines_regex.match(line):
                match_count += 1

            if not owner:
                owner_match = describe_owner_regex.match(line)
                if owner_match:
                    owner = owner_match.group(1).lower()

        if skip_cl:
            owner = "<OUTSIDE TIME SPAN>"

        # Idea 1: The more files a CL changes, the less important that CL is to each
        #         file. Therefore, the author of the CL gains less ownership of each
        #         file than an author targeting fewer files.
        # Idea 2: The more lines a CL changes in a file, the more important the CL
        #         is to the file and therefore the more ownership the author gets.
        if match_count == 0:
            score = 0
        else:
            # TODO: This score should only divide by the number of files that
            # are not in the set of files we're querying ownership for. A CL
            # that affects a lot of files but where all are among the files we
            # are querying for should not be penalized.
            score = linecount * 1.0 / math.sqrt(match_count)

        assert(owner != "")
        assert(owner is not None)

        if owner in owner_score_map:
            owner_score_map[owner] += score
        else:
            owner_score_map[owner] = score

    if start_date_skip_count > 0:
        assert(start_date is not None)
        print_warning("Skipped %d of %d CLs because of the provided start date %s." % (start_date_skip_count, cl_count, start_date.strftime("%Y/%m/%d")))

        if start_date_skip_count == cl_count:
            print_warning("Skipped all CLs. Nothing to do. Exiting.")

    return owner_score_map

def sort_ownership_score(owner_score_map):
    return sorted(owner_score_map.items(), key=lambda item: item[1], reverse=True)

def combine_ownership_score(left_owner_map, right_owner_map):
    out_map = {}

    for in_map in [left_owner_map, right_owner_map]:
        for key in in_map:
            if key in out_map:
                out_map[key] += in_map[key]
            else:
                out_map[key] = in_map[key]

    return out_map

def normalize_scores(scores):
    total_score = 0.0
    for owner, score in scores:
        total_score += score

    if total_score == 0:
        return scores

    normalized_scores = []
    for owner, score in scores:
        new_score = 100.0 * score / total_score
        normalized_scores.append((owner, new_score))

    return normalized_scores

def print_scores(scores, normalized):
    if normalized:
        scores = normalize_scores(scores)

        for owner,score in scores:
            print("%5.2f%% %s" % (score, owner))
    else:
        for owner,score in scores:
            print("%.2f %s" % (score, owner))

def expand_directory(path, print_info, print_error):
    """Strip trailing slashes and instead determine using "p4 where" combined
    with os.path.isdir if a path is a directory or not. Paths that do point at
    directories get ... added to the end (delimited by the system path
    separator)."""

    # Nothing to do if the user already indicated clearly that they intend to
    # query a directory.
    if path.endswith("\\...") or path.endswith("/..."):
        return path

    output_path = path

    while output_path.endswith("\\") or output_path.endswith("/"):
        output_path = output_path.rstrip("\\").rstrip("/")

    start_time = time.time()

    where_result = subprocess.run(["p4", "-z", "tag", "where", output_path], capture_output=True)
    where_stdout = where_result.stdout.decode("utf-8")

    global p4_where_walltime
    p4_where_walltime += time.time() - start_time
    global p4_where_call_count
    p4_where_call_count += 1

    if where_result.returncode != 0:
        where_stderr = where_result.stderr.decode("utf-8")
        msg = "p4 where failed, giving up. Output:\n%s\n%s" % (where_stdout, where_stderr)
        print_error(msg)
        sys.exit(1)

    local_path = None
    path_regex = re.compile(r'^... path (.*)')
    for line in where_stdout.splitlines():
        m = path_regex.match(line)
        if not m:
            continue
        local_path = m.group(1)

    if os.path.isdir(local_path):
        output_path = os.path.join(output_path, "...")

    if output_path != path:
        print_info("Adjusted the provided path. Will use \"%s\"" % output_path)

    return output_path


class Authors(flow.cmd.Cmd):
    """List the authors of a given path.
    Give each author a score based on how many lines they have changed under the
    given path. An author's score is increased if they changed many lines (more
    important author) and decreased if many files where involved in their
    changelists (less important author because the file was not as central to
    their change)."""
    path                = flow.cmd.Arg(str, "Path to the file. Supports relative and absolute local paths. Supports Perforce paths (including those ending in ...).")
    after               = flow.cmd.Opt("", "Only count changes after a specified date (following the format YYYY/mm/dd).")
    pastyear            = flow.cmd.Opt(False, "Only count changes from the past year.")
    pasttwoyears        = flow.cmd.Opt(False, "Only count changes from the past two years.")
    time                = flow.cmd.Opt(False, "Print performance timings.")
    follow_integrations = flow.cmd.Opt(False, "Follow integrations when annotating file lines by passing -I to p4 annotate. The default is to follow branches by passing -i to p4 annotate.")
    normalize_scores    = flow.cmd.Opt(True, "Normalize scores before printing them (default).")

    def main(self):
        start_time = time.time()

        start_date = None
        if len(self.args.after) > 0:
            try:
                start_date = datetime.datetime.strptime(self.args.after, "%Y/%m/%d")
            except:
                self.print_error("Failed to parse your provided date \"%s\" as a YYYY/mm/dd date. Exiting." % self.args.after)
                sys.exit(1)
        elif self.args.pastyear:
            start_date = datetime.datetime.now() + datetime.timedelta(days=-365)
        elif self.args.pasttwoyears:
            start_date = datetime.datetime.now() + datetime.timedelta(days=-2*365)

        if start_date is not None:
            self.print_info("Only counting changes since %s." % start_date.strftime("%Y/%m/%d"))

        path = expand_directory(self.args.path, self.print_info, self.print_error)

        cl_linecount_map = query_cl_linecount(path, self.print_error, self.print_warning, follow_integrations=self.args.follow_integrations)
        owner_score = query_owner_scores(cl_linecount_map, self.print_info, self.print_warning, self.print_error, start_date)
        scores = sort_ownership_score(owner_score)

        print_scores(scores, self.args.normalize_scores)

        run_time = time.time() - start_time

        if self.args.time:
            self.print_info("Total runtime: %5.2f s" % run_time)
            self.print_info("  p4 annotate: %5.2f s (%d %s)" % (p4_annotate_walltime, p4_annotate_call_count, "call" if p4_annotate_call_count == 1 else "calls"))
            self.print_info("  p4 describe: %5.2f s (%d %s)" % (p4_describe_walltime, p4_describe_call_count, "call" if p4_describe_call_count == 1 else "calls"))
            self.print_info("     p4 where: %5.2f s (%d %s)" % (p4_where_walltime, p4_where_call_count, "call" if p4_where_call_count == 1 else "calls"))

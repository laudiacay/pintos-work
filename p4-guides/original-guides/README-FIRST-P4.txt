TA NOTE: The materials in this folder are only for those who want to take a look
         at the non-trimmed version of the guidance. You don't need to read any
         of the materials here for current trimmed P4. Implementing additional 
         features (e.g., buffer cache) won't bring you any bonus point.

Project 4 Guidelines

    (IMPORTANT NOTE: Because P4 deadline is in the final week, we want to make
    sure there is no unexpected grading process. As usual, we will release the
    grading script. Pay attention to the following aspects:)

    ** DESIGN DOC AND PROJECT PARTICIPATION **

    In your design document, in the first two lines, please write the
    contribution of each of the member in the group, with the following format:

    X% Partner #1: Firstname Lastname, CNetID
    Y% Partner #2: Firstname Lastname, CNetID

    You should run the P4 grading script **write the expected grade in the 3rd
    line in your design document**.  This way, if we see any discrepancy, we can
    contact you ASAP.)

    (Please build P4 from P2, don't enable VM parts. This way, you can avoid
    propagating P3 bugs to this project and your life will be easier. There
    won't be 5% extra credit if you build P4 based on P3. Contact the TA for
    approval if you have specific needs to build P4 on top of P3.)

    (You won't receive a 20% penalty for p4 if you use the p2 code provided by
    us)


In this directory, we provide some code segments to speed you up with the
project.  It is totally permissible to simply copy the code segments bit by
bit.  DON'T COPY THE WHOLE FILE DIRECTLY -- these files are not compilable.
Please add new features bit by bit!

As usual, you are NOT required to follow our guidelines.  You can design
and implement your own solution in anyway you like as long as you pass the
tests.

Please IGNORE the readahead and periodic flush requirements.  You can get
100% score without implementing these features.  (If this is wrong, please
let us know).  If you want to add these features for fun, please feel free
to do so.

You should use two inode structures: in-memory (struct inode) and on-disk
inode (struct inode_disk). In-memory inode can have more information (as
many as you want) compared to on-disk inode.

Skim through all the test files, so you can create your own implementation
roadmap.

The first couple of test files do not need persistence. Hence, you can
create a simple buffer cache layer that does not perform eviction.

Implement directory first, don't try to implement multi-level indexed
until it's needed.

Writes to disk ONLY happen under sync(), eviction, and shut down.  Thus, do
NOT try to manually write your block to the disk on file close and other
places.  Initially, you will fail all the tests that create big files, big
directories, and big working set.  Later, you can add the necessary
features bit by bit.

Again, add new features incrementally, commit, then repeat.  Don't try to
implement all the requirements all together.

In this project, you will also use condition variables.  For more, please
read the Appendix A.3.4 Monitors.

C programming language allows you to write 'goto' construct.
If you have a complex logic (e.g. in cache_lock), you can
use 'goto' like this:

int func () {

 try_again:
   // ...
   if (someCondition) {
     goto try_again;
   }
   else {
     goto done;
   }

 done:
   return result;
}



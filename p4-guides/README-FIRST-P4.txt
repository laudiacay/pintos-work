
Project 4 Guidelines

    To graduating students: first, please let us know that you are graduating this quarter by 
    emailing P4 TA (hmz20000@uchicago.edu for Fall 2020) and cc the instructor, if you have 
    not done so.
    As the P4 deadline for graduating students is earlier, the grading policy for graduating 
    students change as follows:
    1. We will first collect a snapshot p4 as submitted on Nov 2nd. The graduating students'
    score will be 100/60 * score from p4 grading script
    2. If the partner in the team is not graduating, he/she can continue working on p4 till
    the regular deadline (Nov 9th). We will pull the updated code and grade it as usual 
    (i.e., no 100/60 bonus).
    

    (IMPORTANT NOTE: 
    A major part of p4 is trimmed, please make sure you read the entire document
    to get every detail. In this trimmed P4, you only need to complete 5.3.1 "Design document",
    5.3.2 "Indexed and Extensible Files", 5.3.3 "Subdirectories" and 5.3.5 "Synchronization". 
    You don't need to do 5.3.4 "Buffer Cache". The original p4 guide is in 
    original-guide folder for people who are interested in the trimmed part, 
    which will NOT be counted in grading. Overall, compared to prior years, we 
    reduce the workload by around 50%.
     
    Because P4 deadline is in the final week, we want to make
    sure there is no unexpected grading process. As usual, we will release the
    grading script. Additionally, there will be NO extension to p4.  The 10% one-day
    late penalty, 20% two-day late penalty etc. NO LONGER applies for p4. p4 has a 
    HARD DEADLINE.

    P4 should be built upon P2. Please do not build P4 on P3. You won't receive
    any penalty for p4 if you use the p2 code provided by us previously for p3.

    

    ** DESIGN DOC AND PROJECT PARTICIPATION **

    In your design document, in the first two lines, please write the
    contribution of each of the member in the group, with the following format:

    X% Partner #1: Firstname Lastname, CNetID
    Y% Partner #2: Firstname Lastname, CNetID

    You should run the P4 grading script **write the expected grade in the 3rd
    line in your design document**.  This way, if we see any discrepancy, we can
    contact you ASAP.)


In this directory, we provide some code segments to speed you up with the
project.  It is totally permissible to simply copy the code segments bit by
bit.  DON'T COPY THE WHOLE FILE DIRECTLY -- these files are not compilable.
Please add new features bit by bit!

As usual, you are NOT required to follow our guidelines.  You can design
and implement your own solution in anyway you like as long as you pass the
tests.

* IMPORTANT *
In this trimmed P4, you only need to complete 5.3.1 "Design document", 
5.3.2 "Indexed and Extensible Files", 5.3.3 "Subdirectories", and 
5.3.5 "Synchronization". You don't need to do 5.3.4 "Buffer Cache".

That is, your filesystem needs to be able to support large files (mainly by 
modifying inode.c and inode.h), subdirectories (mainly in directory.h, directory.c 
and syscall related files in userprog), and add lock mechanisms to ensure that 
various structures and methods can be called/accessed concurrently (there is no 
requirement on the granularity of these locks, so it is okay to use even just 
one big lock as long as your code passes all tests). 

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






// declaring the states of the watch
enum State {
  IDLE,
  RUNNING,
  PAUSED
};
// declaring the first state while bootup
State state = IDLE ;
// setting the button state high before starting
int b1_prev = HIGH ;
int b2_prev = HIGH ;
int b3_prev = HIGH ;
int b4_prev = HIGH ;


// defined the buttons here
#define B1 4
#define B2 18
#define B3 19
#define B4 23

// declaring the variables for the time logic
unsigned long start_time = 0;
unsigned long elapsed_time = 0;


void setup() {
  //starting the serial connection
  Serial.begin(115200);
  // put your setup code here, to run once:
  pinMode(B1,INPUT_PULLUP);
  pinMode(B2,INPUT_PULLUP);
  pinMode(B3,INPUT_PULLUP);
  pinMode(B4,INPUT_PULLUP);
}

void loop() {
  // put your main code here, to run repeatedly
  unsigned long now = millis(); // need to know what is the time now ( after rebooting ) 

  int b1 = digitalRead(B1);
  int b2 = digitalRead(B2);
  int b3 = digitalRead(B3);
  int b4 = digitalRead(B4);
  // now buttons pressing checked
  if ( b1 == LOW && b1_prev == HIGH ){
    // HERE THE CODE FOR BUTTON 1
    if ( state == IDLE ) {
      start_time = millis(); // the button press point is the reference
      elapsed_time = 0 ; // the time after setting start time at this point will be zero right
      state = RUNNING ; // after click the timer should start
    }
    else if ( state == RUNNING ){
      state = PAUSED ;
    }
    else if ( state == PAUSED ) {
      state = RUNNING ;
    }
  }
  

  // Print the state in which the watch is in right now
  Serial.println(state);
  // AT THE END OF EVERY LOOP THE BUTTON ASKS FOR INPUT
  b1_prev = b1;
  b2_prev = b2;
  b3_prev = b3;
  b4_prev = b4;
   
}

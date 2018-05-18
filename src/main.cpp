/* Game Controller */
#include <mbed.h>
#include <EthernetInterface.h>
#include <rtos.h>
#include <mbed_events.h>

#include <FXOS8700Q.h>
#include <C12832.h>

/* display */
C12832 lcd(D11, D13, D12, D7, D10);

/* event queue and thread support */
Thread dispatch;
EventQueue periodic;

/* Accelerometer */
I2C i2c(PTE25, PTE24);
FXOS8700QAccelerometer acc(i2c, FXOS8700CQ_SLAVE_ADDR1);

/* Input from Potentiometers */
AnalogIn left(A0); /* Variable Throttle */

/* LED outputs*/
enum {red, green, blue};
DigitalOut LED[] = {
  DigitalOut(PTB22,1), /* Red if fuel goes below 50 / if lander crashes */
  DigitalOut(PTE26,1), /* Green if You land sucessfully */
  DigitalOut(PTB21 ,1) /* Blue if you are above 50 fuel */
};
/* Throttle and Roll control */
DigitalIn joy_up(A2); /*Throttle*/

DigitalIn joy_left(A4); /*roll left*/
DigitalIn joy_right(A5); /*roll right*/

/* Speaker Output */
PwmOut speaker(D6); /* used if lander crashes */

/* Function to check if button is pressed */
bool isPressed(DigitalIn button) {
  if (button.read()) {
    return true;
  }
  else {
    return false;
  }
}

/* variables to hold the users desired actions.*/
float roll = 0;
float throttle = 0;

/* Task for polling sensors */
void user_input(void){
    motion_data_units_t a;
    acc.getAxis(a);
    /* Digital control for throttle from joystick */
    if(isPressed(joy_up)){
      throttle = 100;
    }
    else {
      /* left potentiometer used for variable throttle contorl*/
      throttle = left.read() * 100;

      // Allow throttle to read 100
      if (throttle >= 99.5) {
        throttle = 100;
      }
    }

    /* Digital roll control from joystick */
    if(isPressed(joy_left)) {
      roll =- 1;
    }
    else if (isPressed(joy_right)) {
      roll =+ 1;
    }
    else {
      /* Angle for accelerometer roll controls */
      float magnitude = sqrt((a.x*a.x) + (a.y*a.y) + (a.z*a.z));
      a.x = a.x/magnitude;

      float angle = asin(a.x);

      /* Roll Deadband */
      if (angle <= 0.1 && angle >= -0.1) {
        angle = 0;
      }

      roll = -(angle);
  }
}

/* States from Lander */
float altitude = 0;
float fuel = 100;
bool isflying = false;
bool iscrashed = false;
int orientation = 0;
int xVelocity = 0;
int yVelocity = 0;

/* hardwire the IP address in here */
SocketAddress lander("192.168.80.9",65200);
SocketAddress dash("192.168.80.9",65250);

EthernetInterface eth;
UDPSocket udp;

/* Task for synchronous UDP communications with lander */
void communications(void){
    SocketAddress source;

    /* formatting message to lander */
    char buffer[512];
    sprintf(buffer, "command:!\nthrottle:%d\nroll:%1.3f",int(throttle),roll);
    /* Sending and recieveing message */
    udp.sendto( lander, buffer, strlen(buffer));
    nsapi_size_or_error_t  n = udp.recvfrom(&source, buffer, sizeof(buffer));
    buffer[n] = '\0';
    /* Unpack incomming message */
    char *nextline, *line;

    for(
      line = strtok_r(buffer, "\r\n", &nextline);
      line != NULL;
      line = strtok_r(NULL, "\r\n", &nextline)
    ) {
      /* Split into key value pairs */
      char *key, *value;
      key = strtok(line, ":");
      value = strtok(NULL, ":");
    /* Digital control for throttle from joystick */
      /* Convert value strings into state variables */
      if(strcmp(key,"altitude")==0) {
        altitude = atof(value);
      }
      else if(strcmp(key, "fuel")==0) {
        fuel = atof(value);
      }
      else if(strcmp(key, "flying")==0) {
        isflying = atoi(value);
      }
      else if(strcmp(key, "crashed")==0) {
        iscrashed = atoi(value);
      }
      else if(strcmp(key, "orientation")==0) {
        orientation = atoi(value);
      }
      else if(strcmp(key, "Vx")==0) {
        xVelocity = atoi(value);
      }
      else if(strcmp(key, "Vy")==0) {
        yVelocity = atoi(value);
      }
    }
  }

/* Task for asynchronous UDP communications with dashboard */
void dashboard(void){
    /* Create and format a message to the Dashboard */
    SocketAddress source;
    char message[512];
  //  sprintf(buffer, "command:=\naltitude:%1.2f\nfuel:%1.2f\nflying:%d\ncrashed:%d\norientation:%d\nVx:%d\nVy:%dthrottle:%f\n",altitude,fuel,isflying,iscrashed,orientation,xVelocity,yVelocity,throttle);
      sprintf(message, "fuel:%1.2f\nthrottle:%f\nroll:%f\naltitude:%1.2f\n",fuel, throttle, roll, altitude);
    /* send the message to the dashboard*/
      udp.sendto( dash, message, strlen(message));
}

int main() {
    acc.enable();

    /* ethernet connection : usually takes a few seconds */
    printf("conecting \n");
    eth.connect();
    /* write obtained IP address to serial monitor */
    const char *ip = eth.get_ip_address();
    printf("IP address is: %s\n", ip ? ip : "No IP");

    /* open udp for communications on the ethernet */
    udp.open( &eth);

    printf("lander is on %s/%d\n",lander.get_ip_address(),lander.get_port() );
    printf("dash   is on %s/%d\n",dash.get_ip_address(),dash.get_port() );

    /* periodic tasks
    call periodic tasks;  */
    periodic.call_every(50, communications);
    periodic.call_every(50, dashboard);
    periodic.call_every(50, user_input);

    /* start event dispatching thread */
    dispatch.start( callback(&periodic, &EventQueue::dispatch_forever) );

    while(1) {
        /* LCD Displays altidue, fuel and velocity */
        if (!iscrashed){
        lcd.locate(0,0);
        lcd.printf("Altitude: %d \nFuel: %d \nVelocity X: %d   Y: %d  ",int(altitude),int(fuel),xVelocity,yVelocity);
        lcd.locate(60,0);
        lcd.printf("Throttle: %d",int(throttle));
        speaker.write(1);
        }
        /* else if crashed clear screen and display 'you have crashed', play sound and red light */
        else{
        lcd.locate(0,0);
        lcd.cls();
        lcd.printf("You have Crashed");
        speaker.period(1.0/440);
        speaker.write(0.5);
        wait(0.25);
        speaker.write(1);
        LED[2].write(1);
        LED[0].write(0);
        break;

        }
        /* Blue Light while fuel above 50 */
        if(fuel > 50){
          LED[0].write(1);
          LED[2].write(0);
        }
        /* Light turns red after fuels goes below 50 */
        else {
          LED[0].write(0);
          LED[2].write(1);
        }
        /* if you land LED goes green and lcd displays you have landed */
        if ((isflying == false) && (iscrashed == false)){
          LED[0].write(1);
          LED[1].write(0);
          speaker.period(1.0/300);
          speaker.write(0.5);
          wait(0.25);
          speaker.write(1);
          lcd.locate(0,0);
          lcd.cls();
          lcd.printf("You have Landed");
        break;

        }

        wait(1);
    }
}
